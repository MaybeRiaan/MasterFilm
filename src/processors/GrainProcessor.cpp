// src/processors/GrainProcessor.cpp
#include "GrainProcessor.h"
#include <cmath>
#include <cstdint>

namespace MasterFilm {

// ── Parameter mapping ─────────────────────────────────────────────────────────

float GrainProcessor::sizeToSigma(float size, float iso)
{
    // Parametric mapping: larger grain at higher ISO, scaled by size slider.
    // σ_base from ISO: rough empirical relationship — grain area scales with ISO.
    // σ_base ≈ k * sqrt(ISO / 100), with k tuned to match published RMS data.
    const float k = 0.35f;
    const float sigmaBase = k * std::sqrt(iso / 100.0f);
    // size [0,1] maps to [0.3, 3.0] × sigmaBase
    const float scale = 0.3f + size * 2.7f;
    return sigmaBase * scale;
}

float GrainProcessor::zoneWeight(float luma) const
{
    // Smooth zone blend:
    //   shadow  → luma < 0.33
    //   mid     → 0.20 – 0.66
    //   highlight → luma > 0.55
    // Using Gaussian-like blending so zones overlap naturally.
    auto gauss = [](float x, float mu, float sig) -> float {
        float d = (x - mu) / sig;
        return std::exp(-0.5f * d * d);
    };

    float wShadow    = gauss(luma, 0.15f, 0.18f) * mParams.shadowWeight;
    float wMid       = gauss(luma, 0.50f, 0.22f) * mParams.midWeight;
    float wHighlight = gauss(luma, 0.85f, 0.18f) * mParams.highlightWeight;

    float total = wShadow + wMid + wHighlight;
    return total > 0.0f ? total : 0.0f;
}

// ── GPU dispatch ──────────────────────────────────────────────────────────────

OfxStatus GrainProcessor::processGPU(OfxImageEffectHandle /*effect*/,
                                     OfxPropertySetHandle /*srcImg*/,
                                     OfxPropertySetHandle /*dstImg*/,
                                     int /*renderSeed*/) const
{
    // TODO: build uniform block from mParams, dispatch grain shader
    // Shader: shaders/glsl/grain.glsl  (GLSL path)
    //         shaders/metal/grain.metal (Metal path)
    return kOfxStatReplyDefault;
}

// ── CPU fallback ──────────────────────────────────────────────────────────────

OfxStatus GrainProcessor::processCPU(const float* src, float* dst,
                                     int width, int height, int nComponents,
                                     int renderSeed) const
{
    // Simple LCG noise as placeholder — replace with proper PSD-filtered noise.
    // TODO: implement frequency-domain grain synthesis matching PSD model.
    auto lcg = [](uint32_t& state) -> float {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 16) / 65536.0f - 0.5f;
    };

    uint32_t state = static_cast<uint32_t>(renderSeed);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * nComponents;
            float luma = 0.0f;
            if (nComponents >= 3)
                luma = 0.2126f * src[idx] + 0.7152f * src[idx+1] + 0.0722f * src[idx+2];
            else
                luma = src[idx];

            float weight = zoneWeight(luma);
            float sigma  = sizeToSigma(mParams.size, mParams.iso);
            float noise  = lcg(state) * mParams.amount * weight * sigma * 0.1f;

            for (int c = 0; c < nComponents; ++c)
                dst[idx + c] = src[idx + c] + noise;
        }
    }
    return kOfxStatOK;
}

} // namespace MasterFilm
