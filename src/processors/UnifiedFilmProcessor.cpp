// src/processors/UnifiedFilmProcessor.cpp
// Unified four-stage photochemical film emulation pipeline.
//
// Signal flow per pixel:
//
//   Stage 1 — Exposure
//     encoded → toLinear(mode) → scene linear
//     scene linear → log2(lin / 0.18) → stops relative to middle grey
//
//   Stage 2 — Negative
//     stops → density (per-channel sigmoid LUT)
//     filmColor blend: lerp R/B density toward G density
//     coupling matrix: 3×3 multiply in density space
//
//   Stage 3 — Timing
//     density += printerLight stop offset (converted from Kodak scale)
//     Neutral lights (25/25/25) = no offset = identity
//
//   Stage 4 — Print (per-channel LUT-based)
//     density_delta = density - exitDMid
//     code_out = samplePrintLUT(density_delta, channelLUT)
//
//     Three per-channel print LUTs bake the entire exit ramp:
//       LINEAR mode: stopsOut = -delta × log₂(10) × printGamma
//                    (all three LUTs identical)
//       2383 mode:   per-channel H&D sigmoid from 2383 datasheet
//                    (each dye layer has own dMin, dMax, gamma, x0)
//     followed by: 0.18 × 2^stopsOut → fromLinear(mode)
//
//     This replaces per-pixel pow/exp/log with a single LUT lookup.
//     In 2383 mode, the per-channel curves produce natural colour
//     separation from the print stock's three dye layers.
//
//   Stage 5 — Zone Color (optional artistic post-process)
//     Zone-weighted hue shift + saturation scaling in HSL
//     Uses correct luma coefficients for the working colour space

#include "UnifiedFilmProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

// ═════════════════════════════════════════════════════════════════════════════
//  Construction & parameter update
// ═════════════════════════════════════════════════════════════════════════════

UnifiedFilmProcessor::UnifiedFilmProcessor(const ToneParams& tone,
                                           const TimingParams& timing,
                                           const PrintParams& print,
                                           const ColorParams& color)
    : mTone(tone), mTiming(timing), mPrint(print), mColor(color)
{
    rebuildLUTs();
    rebuildTimingOffsets();
    mPrintLUTValid = false;  // will be built on first processCPU call
}

void UnifiedFilmProcessor::setParams(const ToneParams& tone,
                                     const TimingParams& timing,
                                     const PrintParams& print,
                                     const ColorParams& color)
{
    mTone   = tone;
    mTiming = timing;
    mPrint  = print;
    mColor  = color;
    rebuildLUTs();
    rebuildTimingOffsets();
    mPrintLUTValid = false;  // force rebuild with current mode
}

// ═════════════════════════════════════════════════════════════════════════════
//  Sigmoid H&D curve — positive characteristic (negative inverted)
// ═════════════════════════════════════════════════════════════════════════════

float UnifiedFilmProcessor::evaluateCurve(float stops, const ChannelCurve& c)
{
    const float k = 4.0f * c.gamma / (c.dMax - c.dMin);
    return c.dMax - (c.dMax - c.dMin) / (1.0f + std::exp(-k * (stops - c.x0)));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Negative LUT bake — stores raw density, no exit ramp
// ═════════════════════════════════════════════════════════════════════════════

void UnifiedFilmProcessor::rebuildLUTs()
{
    for (int i = 0; i < kLUTSize; ++i)
    {
        const float stops = kStopsMin
            + (static_cast<float>(i) / static_cast<float>(kLUTSize - 1))
            * kStopsRange;

        mLUT_R[i] = evaluateCurve(stops, mTone.red);
        mLUT_G[i] = evaluateCurve(stops, mTone.green);
        mLUT_B[i] = evaluateCurve(stops, mTone.blue);
    }

    // Density at 0 stops (middle grey) — per-channel anchors
    mDMidR = evaluateCurve(0.0f, mTone.red);
    mDMidG = evaluateCurve(0.0f, mTone.green);
    mDMidB = evaluateCurve(0.0f, mTone.blue);

    // Coupled dMid — apply coupling matrix to the raw dMid triplet.
    const auto& m = mColor.couplingMatrix;
    mCoupledDMidR = m[0] * mDMidR + m[1] * mDMidG + m[2] * mDMidB;
    mCoupledDMidG = m[3] * mDMidR + m[4] * mDMidG + m[5] * mDMidB;
    mCoupledDMidB = m[6] * mDMidR + m[7] * mDMidG + m[8] * mDMidB;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Timing offsets — Kodak printer light scale to stop offsets
// ═════════════════════════════════════════════════════════════════════════════

void UnifiedFilmProcessor::rebuildTimingOffsets()
{
    mTimingOffsetR = (mTiming.printerLightR - 25.0f) / 12.0f;
    mTimingOffsetG = (mTiming.printerLightG - 25.0f) / 12.0f;
    mTimingOffsetB = (mTiming.printerLightB - 25.0f) / 12.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Print LUT — bakes the entire exit ramp into per-channel lookup tables
// ═════════════════════════════════════════════════════════════════════════════
//
// Input:  density delta (density - dMid), range [kDeltaMin, kDeltaMax]
// Output: encoded code value (ACEScct or DWG)
//
// LINEAR MODE: single transfer function shared across all three channels.
//   stopsOut = rawStops × printGamma
//   All three LUTs contain identical values.
//
// 2383 MODE: per-channel H&D sigmoid curves from Kodak 2383 datasheet.
//   Each channel evaluates its own print sigmoid (evaluateCurve with
//   2383 parameters), producing different LUTs per dye layer:
//     Red  / cyan dye:    dMin=0.06, dMax=3.60, γ=0.753, x0=-1.05
//     Green/ magenta dye: dMin=0.06, dMax=3.90, γ=0.753, x0=-1.33
//     Blue / yellow dye:  dMin=0.06, dMax=4.20, γ=0.813, x0=-1.51
//
//   The per-channel dMax and gamma differences create natural colour
//   separation — the print stock adds its own colour signature on top
//   of the negative's character.
//
//   The evaluateCurve function (inverted sigmoid) has the correct
//   orientation in the rawStops domain: bright areas (positive rawStops)
//   produce low print density → high transmission → bright output.
//
// Rebuilt when print params or ColorSpaceMode change.

void UnifiedFilmProcessor::rebuildPrintLUT(ColorSpaceMode mode) const
{
    const float printGamma = mPrint.printGamma;
    const bool  useCurve   = mPrint.usePrintCurve;

    // In 2383 mode, compute per-channel print dMid (density at rawStops=0)
    float printDMidR = 0.0f, printDMidG = 0.0f, printDMidB = 0.0f;
    if (useCurve)
    {
        printDMidR = evaluateCurve(0.0f, mPrint.printRed);
        printDMidG = evaluateCurve(0.0f, mPrint.printGreen);
        printDMidB = evaluateCurve(0.0f, mPrint.printBlue);
    }

    for (int i = 0; i < kPrintLUTSize; ++i)
    {
        const float delta = kDeltaMin
            + (static_cast<float>(i) / static_cast<float>(kPrintLUTSize - 1))
            * kDeltaRange;

        // Beer-Lambert: negative density delta → raw exposure stops
        const float rawStops = -delta * kLog2of10;

        if (useCurve)
        {
            // ── 2383 per-channel sigmoid ─────────────────────────────────
            // Each channel: rawStops → print sigmoid → print density delta
            //             → Beer-Lambert → linear → re-encode

            // Red / cyan dye
            {
                const float printD = evaluateCurve(rawStops, mPrint.printRed);
                const float printDelta = printD - printDMidR;
                const float stopsOut = -printDelta * kLog2of10;
                const float linOut = 0.18f * std::pow(2.0f, stopsOut);
                mPrintLUT_R[i] = CST::fromLinear(std::max(linOut, 1e-10f), mode);
            }

            // Green / magenta dye
            {
                const float printD = evaluateCurve(rawStops, mPrint.printGreen);
                const float printDelta = printD - printDMidG;
                const float stopsOut = -printDelta * kLog2of10;
                const float linOut = 0.18f * std::pow(2.0f, stopsOut);
                mPrintLUT_G[i] = CST::fromLinear(std::max(linOut, 1e-10f), mode);
            }

            // Blue / yellow dye
            {
                const float printD = evaluateCurve(rawStops, mPrint.printBlue);
                const float printDelta = printD - printDMidB;
                const float stopsOut = -printDelta * kLog2of10;
                const float linOut = 0.18f * std::pow(2.0f, stopsOut);
                mPrintLUT_B[i] = CST::fromLinear(std::max(linOut, 1e-10f), mode);
            }
        }
        else
        {
            // ── Linear mode — shared across all channels ─────────────────
            const float stopsOut = rawStops * printGamma;
            const float linOut = 0.18f * std::pow(2.0f, stopsOut);
            const float code = CST::fromLinear(std::max(linOut, 1e-10f), mode);
            mPrintLUT_R[i] = code;
            mPrintLUT_G[i] = code;
            mPrintLUT_B[i] = code;
        }
    }

    mPrintLUTMode  = mode;
    mPrintLUTValid = true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  LUT samplers — linear interpolation
// ═════════════════════════════════════════════════════════════════════════════

float UnifiedFilmProcessor::sampleLUT(float stops,
    const std::array<float, kLUTSize>& lut) const
{
    const float norm = (std::clamp(stops, kStopsMin, kStopsMax) - kStopsMin) / kStopsRange;
    const float fi   = norm * static_cast<float>(kLUTSize - 1);

    const int   lo   = static_cast<int>(fi);
    const int   hi   = std::min(lo + 1, kLUTSize - 1);
    const float frac = fi - static_cast<float>(lo);

    return lut[lo] + frac * (lut[hi] - lut[lo]);
}

float UnifiedFilmProcessor::samplePrintLUT(float densityDelta,
    const std::array<float, kPrintLUTSize>& lut) const
{
    const float norm = (std::clamp(densityDelta, kDeltaMin, kDeltaMax) - kDeltaMin) / kDeltaRange;
    const float fi   = norm * static_cast<float>(kPrintLUTSize - 1);

    const int   lo   = static_cast<int>(fi);
    const int   hi   = std::min(lo + 1, kPrintLUTSize - 1);
    const float frac = fi - static_cast<float>(lo);

    return lut[lo] + frac * (lut[hi] - lut[lo]);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Coupling matrix — applied in density space
// ═════════════════════════════════════════════════════════════════════════════

void UnifiedFilmProcessor::applyCouplingDensity(float& dR, float& dG, float& dB) const
{
    const auto& m = mColor.couplingMatrix;
    const float nr = m[0] * dR + m[1] * dG + m[2] * dB;
    const float ng = m[3] * dR + m[4] * dG + m[5] * dB;
    const float nb = m[6] * dR + m[7] * dG + m[8] * dB;
    dR = nr;
    dG = ng;
    dB = nb;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Zone colour — artistic post-process (hue + saturation)
// ═════════════════════════════════════════════════════════════════════════════

float UnifiedFilmProcessor::shadowBlend(float luma)
{
    float t = std::max(0.0f, 1.0f - luma / 0.35f);
    return t * t;
}

float UnifiedFilmProcessor::midBlend(float luma)
{
    float d = (luma - 0.5f) / 0.3f;
    return std::exp(-0.5f * d * d);
}

float UnifiedFilmProcessor::highlightBlend(float luma)
{
    float t = std::max(0.0f, (luma - 0.65f) / 0.35f);
    return t * t;
}

void UnifiedFilmProcessor::rgbToHsl(float r, float g, float b,
                                     float& h, float& s, float& l)
{
    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    l = (maxC + minC) * 0.5f;

    float delta = maxC - minC;
    if (delta < 1e-6f) {
        h = s = 0.0f;
        return;
    }

    s = delta / (1.0f - std::abs(2.0f * l - 1.0f));

    if (maxC == r)
        h = std::fmod((g - b) / delta, 6.0f);
    else if (maxC == g)
        h = (b - r) / delta + 2.0f;
    else
        h = (r - g) / delta + 4.0f;

    h *= 60.0f;
    if (h < 0.0f) h += 360.0f;
}

void UnifiedFilmProcessor::hslToRgb(float h, float s, float l,
                                     float& r, float& g, float& b)
{
    float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c * 0.5f;

    float r1, g1, b1;
    int sector = static_cast<int>(h / 60.0f) % 6;
    switch (sector) {
        case 0: r1=c; g1=x; b1=0; break;
        case 1: r1=x; g1=c; b1=0; break;
        case 2: r1=0; g1=c; b1=x; break;
        case 3: r1=0; g1=x; b1=c; break;
        case 4: r1=x; g1=0; b1=c; break;
        default: r1=c; g1=0; b1=x; break;
    }
    r = r1 + m;
    g = g1 + m;
    b = b1 + m;
}

void UnifiedFilmProcessor::getLumaCoeffs(ColorSpaceMode mode,
                                          float& wR, float& wG, float& wB)
{
    switch (mode) {
    case ColorSpaceMode::ACEScct:
        wR = 0.2722287168f;
        wG = 0.6740817658f;
        wB = 0.0536895174f;
        break;
    case ColorSpaceMode::DaVinciWideGamut:
        wR = 0.2820f;
        wG = 0.6709f;
        wB = 0.0471f;
        break;
    default:
        wR = 0.2722287168f;
        wG = 0.6740817658f;
        wB = 0.0536895174f;
        break;
    }
}

void UnifiedFilmProcessor::applyZoneColor(float& r, float& g, float& b,
                                           float luma, ColorSpaceMode /*mode*/) const
{
    float ws = shadowBlend(luma);
    float wm = midBlend(luma);
    float wh = highlightBlend(luma);

    float hueShift = ws * mColor.hueShadowShift
                   + wm * mColor.hueMidShift
                   + wh * mColor.hueHighlightShift;

    float satScale = 1.0f
        + ws * (mColor.satShadow    - 1.0f)
        + wm * (mColor.satMid       - 1.0f)
        + wh * (mColor.satHighlight - 1.0f);

    if (std::abs(hueShift) < 0.01f && std::abs(satScale - 1.0f) < 0.001f)
        return;

    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);

    h = std::fmod(h + hueShift + 360.0f, 360.0f);
    s = std::clamp(s * satScale, 0.0f, 1.0f);

    hslToRgb(h, s, l, r, g, b);
}

// ═════════════════════════════════════════════════════════════════════════════
//  CPU processing — four-stage pipeline
// ═════════════════════════════════════════════════════════════════════════════

OfxStatus UnifiedFilmProcessor::processCPU(const float* src, float* dst,
    int width, int height,
    int nComponents,
    ColorSpaceMode mode) const
{
    // ── Rebuild print LUT if mode changed or params invalidated ──────────
    if (!mPrintLUTValid || mPrintLUTMode != mode)
        rebuildPrintLUT(mode);

    const int   nPixels   = width * height;
    const float filmColor = mTone.filmColor;

    // Raw (pre-coupling) dMid — used for filmColor blending
    const float dMidR = mDMidR;
    const float dMidG = mDMidG;
    const float dMidB = mDMidB;

    // Coupled dMid — used in exit ramp so middle grey is preserved
    const float cDMidR = mCoupledDMidR;
    const float cDMidG = mCoupledDMidG;
    const float cDMidB = mCoupledDMidB;

    // Precomputed timing offsets (0 at neutral 25/25/25)
    const float timingR = mTimingOffsetR;
    const float timingG = mTimingOffsetG;
    const float timingB = mTimingOffsetB;

    // Check if coupling matrix is identity (skip multiply if so)
    const auto& cm = mColor.couplingMatrix;
    const bool couplingIsIdentity =
        cm[0] == 1.0f && cm[1] == 0.0f && cm[2] == 0.0f &&
        cm[3] == 0.0f && cm[4] == 1.0f && cm[5] == 0.0f &&
        cm[6] == 0.0f && cm[7] == 0.0f && cm[8] == 1.0f;

    // Check if timing is neutral (skip offset if so)
    const bool timingIsNeutral =
        timingR == 0.0f && timingG == 0.0f && timingB == 0.0f;

    // Check if zone colour adjustments are all at defaults (skip if so)
    const bool zoneColorIsNeutral =
        mColor.hueShadowShift == 0.0f &&
        mColor.hueMidShift == 0.0f &&
        mColor.hueHighlightShift == 0.0f &&
        mColor.satShadow == 1.0f &&
        mColor.satMid == 1.0f &&
        mColor.satHighlight == 1.0f;

    // Luma coefficients for zone colour (matched to working space)
    float lumaWR, lumaWG, lumaWB;
    getLumaCoeffs(mode, lumaWR, lumaWG, lumaWB);

    constexpr float kFloor      = 1e-10f;
    constexpr float kMiddleGrey = 0.18f;

    auto processPixel = [&](const float* s, float* d, int nc)
    {
        // ── Stage 1: Exposure ────────────────────────────────────────────
        const float rLin = CST::toLinear(s[0], mode);
        const float gLin = CST::toLinear(s[1], mode);
        const float bLin = CST::toLinear(s[2], mode);

        const float rStops = std::log2(std::max(rLin, kFloor) / kMiddleGrey);
        const float gStops = std::log2(std::max(gLin, kFloor) / kMiddleGrey);
        const float bStops = std::log2(std::max(bLin, kFloor) / kMiddleGrey);

        // ── Stage 2: Negative ────────────────────────────────────────────
        float rDensity = sampleLUT(rStops, mLUT_R);
        float gDensity = sampleLUT(gStops, mLUT_G);
        float bDensity = sampleLUT(bStops, mLUT_B);

        // Film color blend — lerp R/B toward green curve at same exposure
        if (filmColor < 1.0f)
        {
            const float gForR = sampleLUT(rStops, mLUT_G);
            const float gForB = sampleLUT(bStops, mLUT_G);
            rDensity = gForR + filmColor * (rDensity - gForR);
            bDensity = gForB + filmColor * (bDensity - gForB);
        }

        // Inter-layer coupling (density-space matrix)
        if (!couplingIsIdentity)
            applyCouplingDensity(rDensity, gDensity, bDensity);

        // ── Stage 3: Timing ──────────────────────────────────────────────
        if (!timingIsNeutral)
        {
            rDensity -= timingR / kLog2of10;
            gDensity -= timingG / kLog2of10;
            bDensity -= timingB / kLog2of10;
        }

        // ── Stage 4: Print (LUT-based) ──────────────────────────────────
        // Compute density delta, then single LUT lookup per channel.
        // Replaces per-pixel pow/exp/log with interpolated table access.
        float exitDMidR = cDMidR;
        float exitDMidG = cDMidG;
        float exitDMidB = cDMidB;

        if (filmColor < 1.0f && !couplingIsIdentity)
        {
            float bR = dMidG + filmColor * (dMidR - dMidG);
            float bG = dMidG;
            float bB = dMidG + filmColor * (dMidB - dMidG);
            exitDMidR = cm[0] * bR + cm[1] * bG + cm[2] * bB;
            exitDMidG = cm[3] * bR + cm[4] * bG + cm[5] * bB;
            exitDMidB = cm[6] * bR + cm[7] * bG + cm[8] * bB;
        }
        else if (filmColor < 1.0f)
        {
            exitDMidR = dMidG + filmColor * (dMidR - dMidG);
            exitDMidG = dMidG;
            exitDMidB = dMidG + filmColor * (dMidB - dMidG);
        }

        d[0] = samplePrintLUT(rDensity - exitDMidR, mPrintLUT_R);
        d[1] = samplePrintLUT(gDensity - exitDMidG, mPrintLUT_G);
        d[2] = samplePrintLUT(bDensity - exitDMidB, mPrintLUT_B);

        // ── Stage 5: Zone Color (optional) ───────────────────────────────
        if (!zoneColorIsNeutral)
        {
            float luma = lumaWR * d[0] + lumaWG * d[1] + lumaWB * d[2];
            applyZoneColor(d[0], d[1], d[2], luma, mode);
        }

        // Alpha passthrough
        if (nc == 4) d[3] = s[3];
    };

    if (nComponents == 4)
    {
        const float* s = src;
        float*       d = dst;
        for (int p = 0; p < nPixels; ++p, s += 4, d += 4)
            processPixel(s, d, 4);
    }
    else if (nComponents == 3)
    {
        const float* s = src;
        float*       d = dst;
        for (int p = 0; p < nPixels; ++p, s += 3, d += 3)
            processPixel(s, d, 3);
    }

    return kOfxStatOK;
}

// ═════════════════════════════════════════════════════════════════════════════
//  GPU data helpers
// ═════════════════════════════════════════════════════════════════════════════

void UnifiedFilmProcessor::getExitDMid(float& r, float& g, float& b) const
{
    const auto& cm = mColor.couplingMatrix;
    const bool isIdentity = couplingIsIdentity();
    const float fc = mTone.filmColor;

    if (fc < 1.0f && !isIdentity)
    {
        float bR = mDMidG + fc * (mDMidR - mDMidG);
        float bG = mDMidG;
        float bB = mDMidG + fc * (mDMidB - mDMidG);
        r = cm[0] * bR + cm[1] * bG + cm[2] * bB;
        g = cm[3] * bR + cm[4] * bG + cm[5] * bB;
        b = cm[6] * bR + cm[7] * bG + cm[8] * bB;
    }
    else if (fc < 1.0f)
    {
        r = mDMidG + fc * (mDMidR - mDMidG);
        g = mDMidG;
        b = mDMidG + fc * (mDMidB - mDMidG);
    }
    else
    {
        r = mCoupledDMidR;
        g = mCoupledDMidG;
        b = mCoupledDMidB;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  GPU stub
// ═════════════════════════════════════════════════════════════════════════════

OfxStatus UnifiedFilmProcessor::processGPU(OfxImageEffectHandle,
                                           OfxPropertySetHandle,
                                           OfxPropertySetHandle) const
{
    return kOfxStatReplyDefault;
}

} // namespace MasterFilm
