// src/processors/ColorProcessor.cpp
#include "ColorProcessor.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace MasterFilm {

// ── Zone blend weights (smooth Gaussian-like regions) ─────────────────────────

float ColorProcessor::shadowBlend(float luma)
{
    // Peaks at 0, fades out by ~0.35
    float t = std::max(0.0f, 1.0f - luma / 0.35f);
    return t * t;
}

float ColorProcessor::midBlend(float luma)
{
    // Peaks at 0.5, zero at 0 and 1
    float d = (luma - 0.5f) / 0.3f;
    return std::exp(-0.5f * d * d);
}

float ColorProcessor::highlightBlend(float luma)
{
    // Peaks at 1, fades out below ~0.65
    float t = std::max(0.0f, (luma - 0.65f) / 0.35f);
    return t * t;
}

// ── RGB ↔ HSL ─────────────────────────────────────────────────────────────────

void ColorProcessor::rgbToHsl(float r, float g, float b,
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

void ColorProcessor::hslToRgb(float h, float s, float l,
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

// ── Coupling matrix ───────────────────────────────────────────────────────────

void ColorProcessor::applyCoupling(float& r, float& g, float& b) const
{
    const auto& m = mParams.couplingMatrix;
    float nr = m[0]*r + m[1]*g + m[2]*b;
    float ng = m[3]*r + m[4]*g + m[5]*b;
    float nb = m[6]*r + m[7]*g + m[8]*b;
    r = nr; g = ng; b = nb;
}

// ── Zone color (hue shift + saturation) ──────────────────────────────────────

void ColorProcessor::applyZoneColor(float& r, float& g, float& b, float luma) const
{
    float ws = shadowBlend(luma);
    float wm = midBlend(luma);
    float wh = highlightBlend(luma);

    // Weighted hue shift (degrees)
    float hueShift = ws * mParams.hueShadowShift
                   + wm * mParams.hueMidShift
                   + wh * mParams.hueHighlightShift;

    // Weighted saturation scale
    float satScale = 1.0f
        + ws * (mParams.satShadow    - 1.0f)
        + wm * (mParams.satMid       - 1.0f)
        + wh * (mParams.satHighlight - 1.0f);

    if (std::abs(hueShift) < 0.01f && std::abs(satScale - 1.0f) < 0.001f)
        return;  // Early exit if effectively no-op

    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);

    h = std::fmod(h + hueShift + 360.0f, 360.0f);
    s = std::clamp(s * satScale, 0.0f, 1.0f);

    hslToRgb(h, s, l, r, g, b);
}

// ── Orange mask: sigmoid curve (duplicates ToneProcessor math) ────────────────

float ColorProcessor::sigmoidCurve(float stops, const ChannelCurve& c)
{
    float k = 4.0f * c.gamma / (c.dMax - c.dMin);
    return c.dMax - (c.dMax - c.dMin) / (1.0f + std::exp(-k * (stops - c.x0)));
}

// ── Orange mask: density → output code (duplicates ToneProcessor exit ramp) ───

float ColorProcessor::densityToCode(float density, float dMid, float printGamma, ColorSpaceMode mode)
{
    constexpr float kLog2of10 = 3.321928f;
    float densityDelta = density - dMid;
    float stopsOut = -densityDelta * kLog2of10 * printGamma;
    float linOut = 0.18f * std::pow(2.0f, stopsOut);
    return CST::fromLinear(std::max(linOut, 1e-10f), mode);
}

// ── Orange mask LUT builder ───────────────────────────────────────────────────

void ColorProcessor::buildOrangeMaskLUT(const ToneParams& tone, float printGamma, ColorSpaceMode mode)
{
    // Pre-compute dMid for each channel (density at stops=0)
    float dMidR = sigmoidCurve(0.0f, tone.red);
    float dMidG = sigmoidCurve(0.0f, tone.green);
    float dMidB = sigmoidCurve(0.0f, tone.blue);

    for (int i = 0; i < 64; ++i) {
        float encoded = mMaskLUTMin + (static_cast<float>(i) / 63.0f) * (mMaskLUTMax - mMaskLUTMin);

        float linear = CST::toLinear(encoded, mode);
        if (linear < 1e-10f) linear = 1e-10f;

        float stops = std::log2(linear / 0.18f);

        // Per-channel density and output code
        float densityR = sigmoidCurve(stops, tone.red);
        float densityG = sigmoidCurve(stops, tone.green);
        float densityB = sigmoidCurve(stops, tone.blue);

        float codeR = densityToCode(densityR, dMidR, printGamma, mode);
        float codeG = densityToCode(densityG, dMidG, printGamma, mode);
        float codeB = densityToCode(densityB, dMidB, printGamma, mode);

        // Correction gains: bring R and B toward G
        float gainR = (codeR > 1e-10f) ? codeG / codeR : 1.0f;
        float gainG = 1.0f;
        float gainB = (codeB > 1e-10f) ? codeG / codeB : 1.0f;

        mMaskLUT[i] = { gainR, gainG, gainB };
    }
}

// ── GPU dispatch ──────────────────────────────────────────────────────────────

OfxStatus ColorProcessor::processGPU(OfxImageEffectHandle,
                                     OfxPropertySetHandle,
                                     OfxPropertySetHandle) const
{
    // TODO: upload coupling matrix as mat3 uniform
    //       upload zone hue/sat as vec3 uniforms
    //       dispatch shaders/glsl/color.glsl
    return kOfxStatReplyDefault;
}

// ── CPU path ──────────────────────────────────────────────────────────────────

OfxStatus ColorProcessor::processCPU(const float* src, float* dst,
                                     int width, int height, int nComponents) const
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * nComponents;

            float r = src[idx + 0];
            float g = src[idx + 1];
            float b = src[idx + 2];
            float a = (nComponents == 4) ? src[idx + 3] : 1.0f;

            // 1. Inter-layer coupling
            applyCoupling(r, g, b);

            // 2. Zone hue + saturation
            float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            applyZoneColor(r, g, b, luma);

            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
            if (nComponents == 4) dst[idx + 3] = a;
        }
    }
    return kOfxStatOK;
}

} // namespace MasterFilm
