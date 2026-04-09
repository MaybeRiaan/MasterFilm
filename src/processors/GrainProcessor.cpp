// src/processors/GrainProcessor.cpp
#include "GrainProcessor.h"
#include <cmath>
#include <cstdint>

namespace MasterFilm {

// ── Hash helpers (file-local) ─────────────────────────────────────────────────
//
// Wang-hash style mixing.  All functions are purely combinational — no state —
// so the same (x, y, seed) always produces the same output regardless of
// traversal order.  Different renderSeed values yield uncorrelated noise.

static uint32_t wangHash(uint32_t seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return seed;
}

// Maps the low 24 bits of a hash to [-1, 1].
static float hashToFloat(uint32_t h)
{
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x800000u) - 1.0f;
}

// Combines pixel coordinates and frame seed into a single hash value.
static uint32_t pixelHash(int x, int y, int seed)
{
    uint32_t h = static_cast<uint32_t>(seed);
    h = wangHash(h ^ static_cast<uint32_t>(x * 2654435761u));
    h = wangHash(h ^ static_cast<uint32_t>(y * 2246822519u));
    return h;
}

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
//
// Grain model:
//   • Hash-based PRNG seeded from (x, y, renderSeed) — stable per pixel per
//     frame, uncorrelated across frames.
//   • Monochromatic base noise; per-channel variation driven by roughness.
//   • Zone weighting: shadows receive more grain than highlights.
//   • Amplitude reference: rmsGranularity / 1000  (datasheet σ_D × 1000
//     converted back to linear density units).
//   • Alpha channel is passed through unchanged.

OfxStatus GrainProcessor::processCPU(const float* src, float* dst,
                                     int width, int height, int nComponents,
                                     int renderSeed) const
{
    // Pre-compute amplitude scale: rmsGranularity is stored as σ_D × 1000,
    // so divide by 1000 to recover density units.
    const float amplitudeScale = mParams.amount * (mParams.rmsGranularity / 1000.0f);

    // Per-channel seed offsets — large primes to keep channels decorrelated.
    // Channel 0 (R) uses the base hash; offsets are xor'd in for G and B.
    static const uint32_t kChannelOffset[3] = {
        0u,
        0xA341316Cu,  // G
        0x62D86197u,  // B
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = (y * width + x) * nComponents;

            // ── Luminance for zone weighting ──────────────────────────────────
            float luma = 0.0f;
            if (nComponents >= 3)
                luma = 0.2126f * src[idx] + 0.7152f * src[idx+1] + 0.0722f * src[idx+2];
            else
                luma = src[idx];

            const float weight = zoneWeight(luma);
            const float noiseScale = amplitudeScale * weight;

            // ── Base (monochromatic) noise ─────────────────────────────────────
            const uint32_t baseHash = pixelHash(x, y, renderSeed);
            const float baseNoise   = hashToFloat(baseHash);  // [-1, 1]

            // ── Per-channel output ────────────────────────────────────────────
            const int colorChannels = (nComponents >= 3) ? 3 : 1;
            for (int c = 0; c < colorChannels; ++c) {
                float channelNoise;
                if (mParams.roughness <= 0.0f) {
                    // Perfectly monochromatic — skip second hash entirely.
                    channelNoise = baseNoise;
                } else {
                    // Derive an independent per-channel hash by xor-ing a
                    // channel-specific constant into the base hash, then
                    // re-mixing to break correlation.
                    const uint32_t chHash = wangHash(baseHash ^ kChannelOffset[c]);
                    const float chNoise   = hashToFloat(chHash);

                    // lerp(base, perChannel, roughness)
                    channelNoise = baseNoise + mParams.roughness * (chNoise - baseNoise);
                }

                dst[idx + c] = src[idx + c] + channelNoise * noiseScale;
            }

            // ── Alpha passthrough ─────────────────────────────────────────────
            if (nComponents == 4)
                dst[idx + 3] = src[idx + 3];

            // ── Mono / single-channel passthrough for nComponents == 1 ────────
            // (already handled: colorChannels == 1 writes dst[idx+0] above)
        }
    }
    return kOfxStatOK;
}

} // namespace MasterFilm
