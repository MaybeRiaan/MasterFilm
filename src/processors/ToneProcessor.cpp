// src/processors/ToneProcessor.cpp
// Per-channel H&D characteristic curve, log-domain pipeline.
//
// Signal flow per pixel:
//   1. Forward CST:      encoded → scene linear
//   2. Log exposure:     scene linear → log2 stops relative to middle grey (0.18)
//   3. H&D curve:        stops → density (sigmoid, per-channel LUT)
//   4. Film color blend: lerp R/B density toward G density
//   5. Exit ramp:        density → output code value (log-domain, no unit crossing)
//
// Exit ramp detail:
//   density_delta = density - dMid          (deviation from middle-grey anchor)
//   stops_out     = -(density_delta / gamma) (negative: more density = less light)
//   stops_out     = -(density_delta / gamma)   (negate: more density = less output light)
//   code_out      = codeMidGrey + stops_out * unitsPerStop

#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    // ── Sigmoid H&D curve ─────────────────────────────────────────────────────────
    // Positive characteristic curve (negative inverted):
    // D = dMax - (dMax - dMin) / (1 + exp(-k * (stops - x0)))
    //
    // Subtracting from dMax flips the curve so that:
    //   high exposure (bright input)  → low density  → bright output
    //   low exposure  (dark input)    → high density  → dark output
    //
    // This models the full negative→positive inversion in the curve shape
    // rather than as a separate sign flip in the exit ramp.
    // k = 4 * gamma / (dMax - dMin)  matches slope at inflection to datasheet gamma
    float ToneProcessor::evaluateCurve(float stops, const ChannelCurve& c)
    {
        const float k = 4.0f * c.gamma / (c.dMax - c.dMin);
        return c.dMax - (c.dMax - c.dMin) / (1.0f + std::exp(-k * (stops - c.x0)));
    }

    // ── LUT bake ──────────────────────────────────────────────────────────────────
    // Stores raw density values — exit ramp applied per-pixel at render time.
    void ToneProcessor::rebuildLUTs()
    {
        for (int i = 0; i < kLUTSize; ++i)
        {
            const float stops = kStopsMin
                + (static_cast<float>(i) / static_cast<float>(kLUTSize - 1))
                * kStopsRange;

            mLUT_R[i] = evaluateCurve(stops, mParams.red);
            mLUT_G[i] = evaluateCurve(stops, mParams.green);
            mLUT_B[i] = evaluateCurve(stops, mParams.blue);
        }

        // Density at 0 stops (middle grey) — per-channel anchors for exit ramp
        mDMidR = evaluateCurve(0.0f, mParams.red);
        mDMidG = evaluateCurve(0.0f, mParams.green);
        mDMidB = evaluateCurve(0.0f, mParams.blue);
    }

    // ── LUT sampler ───────────────────────────────────────────────────────────────
    float ToneProcessor::sampleLUT(float stops,
                                   const std::array<float, kLUTSize>& lut) const
    {
        const float norm = (std::clamp(stops, kStopsMin, kStopsMax) - kStopsMin) / kStopsRange;
        const float fi   = norm * static_cast<float>(kLUTSize - 1);

        const int lo   = static_cast<int>(fi);
        const int hi   = std::min(lo + 1, kLUTSize - 1);
        const float frac = fi - static_cast<float>(lo);

        return lut[lo] + frac * (lut[hi] - lut[lo]);
    }

    // ── Log-domain exit ramp ──────────────────────────────────────────────────────
    // Converts a density value to an output code value without leaving log space.
    // The negative sign accounts for the negative→positive inversion:
    // higher density on the negative means less light in the positive.
    float ToneProcessor::densityToCode(float density, float dMid,
                                       float gamma,
                                       float codeMidGrey,
                                       float unitsPerStop)
    {
        // density increases with exposure (negative behaviour).
        // To produce a positive image, subtract density FROM dMid —
        // this inverts the negative: high density → below anchor → darker output.
        // The film's characteristic shape is preserved in the non-linearity
        // of how density deviates from dMid across the stop range.
        // density - dMid is positive for bright inputs, negative for dark.
        // Dividing by gamma converts density units back to stop units.
        // Result: bright inputs produce positive stopsOut (stay bright, compressed),
        //         dark inputs produce negative stopsOut (stay dark, lifted at toe).
        // Film negative: density increases with exposure.
        // To produce a positive image we invert — more density above the anchor
        // means less output light. Negating stopsOut achieves this:
        // bright input → high density → negative stopsOut → output below anchor → compressed highlight
        // dark input   → low density  → positive stopsOut → output above anchor → lifted shadow (toe)
        const float densityDelta = density - dMid;
        const float stopsOut     = -(densityDelta / gamma);
        return codeMidGrey + stopsOut * unitsPerStop;
    }

    // ── Color space anchors ───────────────────────────────────────────────────────

    float ToneProcessor::getCodeMidGrey(ColorSpaceMode mode)
    {
        switch (mode)
        {
        case ColorSpaceMode::ACEScct:          return 0.4135f;
        case ColorSpaceMode::DaVinciWideGamut: return 0.5000f;
        }
        return 0.4135f;
    }

    float ToneProcessor::getUnitsPerStop(ColorSpaceMode mode)
    {
        switch (mode)
        {
        case ColorSpaceMode::ACEScct:          return kACESPerStop;
        case ColorSpaceMode::DaVinciWideGamut: return kDWGPerStop;
        }
        return kACESPerStop;
    }

    // ── CPU processing ────────────────────────────────────────────────────────────
    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents,
        ColorSpaceMode mode) const
    {
        const int   nPixels      = width * height;
        const float filmColor    = mParams.filmColor;
        const float codeMidGrey  = getCodeMidGrey(mode);
        const float unitsPerStop = getUnitsPerStop(mode);

        // Per-channel gammas — needed by exit ramp at pixel level.
        // These may have been scaled by LiteUI's gammaScale, so read
        // from mParams rather than hardcoding datasheet values.
        const float gammaR = mParams.red.gamma;
        const float gammaG = mParams.green.gamma;
        const float gammaB = mParams.blue.gamma;

        const float dMidR = mDMidR;
        const float dMidG = mDMidG;
        const float dMidB = mDMidB;

        constexpr float kFloor      = 1e-10f;
        constexpr float kMiddleGrey = 0.18f;

        auto processPixel = [&](const float* s, float* d, int nc)
        {
            // 1. Forward CST → scene linear
            const float rLin = CST::toLinear(s[0], mode);
            const float gLin = CST::toLinear(s[1], mode);
            const float bLin = CST::toLinear(s[2], mode);

            // 2. Scene linear → log2 stops relative to middle grey
            const float rStops = std::log2(std::max(rLin, kFloor) / kMiddleGrey);
            const float gStops = std::log2(std::max(gLin, kFloor) / kMiddleGrey);
            const float bStops = std::log2(std::max(bLin, kFloor) / kMiddleGrey);

            // 3. H&D curve → density (per-channel LUT)
            float rDensity = sampleLUT(rStops, mLUT_R);
            float gDensity = sampleLUT(gStops, mLUT_G);
            float bDensity = sampleLUT(bStops, mLUT_B);

            // 4. Film color blend — lerp R/B toward green curve at same exposure
            float blendDMidR = dMidR;
            float blendDMidB = dMidB;

            if (filmColor < 1.0f)
            {
                const float gForR = sampleLUT(rStops, mLUT_G);
                const float gForB = sampleLUT(bStops, mLUT_G);
                rDensity   = gForR + filmColor * (rDensity - gForR);
                bDensity   = gForB + filmColor * (bDensity - gForB);
                blendDMidR = dMidG + filmColor * (dMidR - dMidG);
                blendDMidB = dMidG + filmColor * (dMidB - dMidG);
            }

            // 5. Log-domain exit ramp → output code values
            d[0] = densityToCode(rDensity, blendDMidR, gammaR, codeMidGrey, unitsPerStop);
            d[1] = densityToCode(gDensity, dMidG,      gammaG, codeMidGrey, unitsPerStop);
            d[2] = densityToCode(bDensity, blendDMidB, gammaB, codeMidGrey, unitsPerStop);

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

    OfxStatus ToneProcessor::processGPU(OfxImageEffectHandle,
        OfxPropertySetHandle,
        OfxPropertySetHandle) const
    {
        return kOfxStatReplyDefault;
    }

} // namespace MasterFilm
