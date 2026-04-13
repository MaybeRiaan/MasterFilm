// src/processors/GrainProcessor.cpp
// Stochastic film grain synthesis — CPU fallback + GPU uniform bridge.
//
// Algorithm overview (matches shaders/glsl/grain.glsl):
//   1. Deterministic temporal seed:  renderSeed = Hash(global_seed + frame_index)
//   2. Per-pixel white noise via Wang hash + Box-Muller
//   3. Morphology shaping: Cubic (Gaussian) or T-Grain (log-normal)
//   4. AR spatial correlation: symmetric FIR over regenerated neighbour noise
//   5. Chroma micro-contrast: mono ↔ per-channel decorrelation blend
//   6. Spectral matrix: 3×3 dye layer crosstalk
//   7. Tonal LUT: luminance → grain amplitude multiplier
//   8. Additive composite in 32-bit linear-light space
#include "GrainProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MasterFilm {

// ── Hash helpers ──────────────────────────────────────────────────────────────
// All functions are purely combinational — no state, no side effects.
// The same (x, y, seed) always produces the same output regardless of
// traversal order or thread scheduling.

uint32_t GrainProcessor::wangHash(uint32_t seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return seed;
}

float GrainProcessor::hashToFloat(uint32_t h)
{
    // Maps the low 24 bits to [0, 1).
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

uint32_t GrainProcessor::pixelHash(int x, int y, uint32_t seed)
{
    uint32_t h = seed;
    h = wangHash(h ^ static_cast<uint32_t>(x * 2654435761u));
    h = wangHash(h ^ static_cast<uint32_t>(y * 2246822519u));
    return h;
}

// ── Parameter mapping ─────────────────────────────────────────────────────────

float GrainProcessor::sizeToSigma(float size, float iso)
{
    const float k = 0.35f;
    const float sigmaBase = k * std::sqrt(iso / 100.0f);
    const float scale = 0.3f + size * 2.7f;
    return sigmaBase * scale;
}

// ── Tonal LUT sampling ───────────────────────────────────────────────────────

float GrainProcessor::sampleTonalLUT(const float* lut, int lutSize, float luma)
{
    float t = std::clamp(luma, 0.0f, 1.0f) * static_cast<float>(lutSize - 1);
    int i0 = static_cast<int>(t);
    int i1 = std::min(i0 + 1, lutSize - 1);
    float frac = t - static_cast<float>(i0);
    return lut[i0] + frac * (lut[i1] - lut[i0]);
}

// ── Morphology shaping ───────────────────────────────────────────────────────

float GrainProcessor::shapeMorphology(float gaussianNoise, int morphType)
{
    if (morphType == static_cast<int>(GrainMorphology::TGrain)) {
        // Log-normal transform: positively skewed distribution.
        // Centre the distribution around zero so it works as additive grain:
        //   X = exp(σ·Z) − exp(σ²/2)    where Z ~ N(0,1), E[X] = 0
        // σ = 0.5 gives a moderate skew (ratio of ~1.7 between
        // 75th and 25th percentile grain amplitudes).
        constexpr float sigma = 0.5f;
        return std::exp(sigma * gaussianNoise)
             - std::exp(0.5f * sigma * sigma);
    }
    // Cubic: Gaussian distribution — return unchanged.
    return gaussianNoise;
}

// ── Box-Muller transform (CPU) ────────────────────────────────────────────────
// Converts two uniform [0,1] samples into a standard normal sample.
static float boxMuller(float u1, float u2)
{
    u1 = std::max(u1, 1e-6f);  // guard against log(0)
    float r = std::sqrt(-2.0f * std::log(u1));
    return r * std::cos(6.28318530718f * u2);
}

// ── GPU dispatch ──────────────────────────────────────────────────────────────

OfxStatus GrainProcessor::processGPU(OfxImageEffectHandle /*effect*/,
                                     OfxPropertySetHandle /*srcImg*/,
                                     OfxPropertySetHandle /*dstImg*/,
                                     int /*renderSeed*/) const
{
    // GPU uniform upload is performed by the render pipeline after
    // glUseProgram().  The uniform names match the GLSL declarations
    // in shaders/glsl/grain.glsl:
    //
    //   uniform float  uRMSGranularity;
    //   uniform int    uMorphologyType;
    //   uniform vec4   uARCoefficients;
    //   uniform float  uARSigma;
    //   uniform mat3   uSpectralMatrix;
    //   uniform float  uChromaMicroContrast;
    //   uniform float  uTonalLUT[64];
    //   uniform uint   uGlobalSeed;
    //   uniform int    uFrameIndex;
    //   uniform float  uGrainSize;
    //   uniform float  uISO;
    //
    // Example upload (caller has the ShaderProgram& from GLSLDispatch):
    //
    //   glUniform1f(prog.loc("uRMSGranularity"),    prof.rms_granularity);
    //   glUniform1i(prog.loc("uMorphologyType"),     prof.morphology_type);
    //   glUniform4fv(prog.loc("uARCoefficients"), 1, prof.ar_coefficients);
    //   glUniform1f(prog.loc("uARSigma"),            prof.ar_sigma);
    //   glUniformMatrix3fv(prog.loc("uSpectralMatrix"), 1, GL_TRUE,
    //                      prof.spectral_matrix);       // row-major → GL_TRUE
    //   glUniform1f(prog.loc("uChromaMicroContrast"), prof.chroma_micro_contrast);
    //   glUniform1fv(prog.loc("uTonalLUT"),    kTonalLUTSize, prof.tonal_lut);
    //   glUniform1ui(prog.loc("uGlobalSeed"),        prof.global_seed);
    //   glUniform1i(prog.loc("uFrameIndex"),         prof.frame_index);
    //   glUniform1f(prog.loc("uGrainSize"),          prof.grain_size);
    //   glUniform1f(prog.loc("uISO"),                prof.iso);

    return kOfxStatReplyDefault;
}

// ── CPU fallback ──────────────────────────────────────────────────────────────
//
// Full stochastic grain synthesis matching the GPU shader algorithm.
// Operates on 32-bit float buffers in scene-linear space.
//
// The grain is additive: dst = src + grain.  Alpha is passed through
// unchanged.  Negative pixel values after grain addition are not clamped
// (the downstream colour space transform handles gamut).

OfxStatus GrainProcessor::processCPU(const float* src, float* dst,
                                     int width, int height, int nComponents,
                                     int /*renderSeed*/) const
{
    const FilmStockProfile& prof = mProfile;

    // ── Step 1: Deterministic per-frame seed ──────────────────────────────
    const uint32_t frameSeed = wangHash(
        prof.global_seed + static_cast<uint32_t>(prof.frame_index));

    // ── Pre-compute amplitude scale ───────────────────────────────────────
    // rms_granularity is σ_D × 1000 → divide by 1000 for density units.
    const float sigma = sizeToSigma(prof.grain_size, prof.iso);
    const float amplitudeBase = (prof.rms_granularity / 1000.0f) * sigma * 0.04f;

    // ── Pre-compute AR normalisation factor ───────────────────────────────
    // The AR FIR convolution multiplies noise by various weights.
    // Normalise so the output variance equals the input variance.
    float arNormSq = prof.ar_sigma * prof.ar_sigma;
    for (int k = 0; k < kMaxAROrder; ++k)
        arNormSq += prof.ar_coefficients[k] * prof.ar_coefficients[k];
    const float arNorm = (arNormSq > 1e-6f) ? 1.0f / std::sqrt(arNormSq) : 1.0f;

    // ── Per-channel hash offsets ──────────────────────────────────────────
    // Large primes keep channels decorrelated.
    static const uint32_t kChOff[3] = { 0u, 0xA341316Cu, 0x62D86197u };

    // ── AR neighbour offsets ──────────────────────────────────────────────
    // Ring 1: 4-connected (N/S/E/W), weight = ar_coefficients[0]
    // Ring 1 diag: 4 diagonals,      weight = ar_coefficients[1]
    // Ring 2: 2-pixel N/S/E/W,       weight = ar_coefficients[2]
    // Ring 2 diag: 2-pixel diag,     weight = ar_coefficients[3]
    struct Offset { int dx, dy; };
    static const Offset ring1[]     = {{1,0},{-1,0},{0,1},{0,-1}};
    static const Offset ring1diag[] = {{1,1},{-1,1},{1,-1},{-1,-1}};
    static const Offset ring2[]     = {{2,0},{-2,0},{0,2},{0,-2}};
    static const Offset ring2diag[] = {{2,2},{-2,2},{2,-2},{-2,-2}};

    const Offset* rings[]    = { ring1, ring1diag, ring2, ring2diag };
    const int     ringSizes[] = { 4, 4, 4, 4 };

    // Lambda: generate a Gaussian noise sample at pixel (px,py) for channel ch.
    // Uses two hash calls → Box-Muller → morphology shaping.
    auto noiseSample = [&](int px, int py, uint32_t chOffset) -> float {
        uint32_t h1 = pixelHash(px, py, frameSeed ^ chOffset);
        uint32_t h2 = wangHash(h1 ^ 0x6C078965u);  // second uniform for Box-Muller
        float u1 = hashToFloat(h1);
        float u2 = hashToFloat(h2);
        float g  = boxMuller(u1, u2);
        return shapeMorphology(g, prof.morphology_type);
    };

    const int colorChannels = (nComponents >= 3) ? 3 : 1;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = (y * width + x) * nComponents;

            // ── Luminance for tonal LUT ──────────────────────────────────
            float luma;
            if (nComponents >= 3)
                luma = 0.2126f * src[idx] + 0.7152f * src[idx+1] + 0.0722f * src[idx+2];
            else
                luma = src[idx];

            // Clamp to [0,1] for LUT indexing (scene-linear can exceed 1.0
            // but grain response is defined over the [0,1] print density range).
            const float tonalWeight = sampleTonalLUT(
                prof.tonal_lut, kTonalLUTSize, std::clamp(luma, 0.0f, 1.0f));

            // ── Per-channel noise with AR correlation ─────────────────────
            float rawNoise[3] = {0.0f, 0.0f, 0.0f};

            // Monochromatic base noise (channel 0, no offset)
            float monoNoise = 0.0f;
            {
                // Centre pixel
                float center = noiseSample(x, y, 0u);
                float arSum = center * prof.ar_sigma;

                // AR neighbour rings
                for (int ring = 0; ring < kMaxAROrder; ++ring) {
                    float coeff = prof.ar_coefficients[ring];
                    if (std::abs(coeff) < 1e-6f) continue;

                    float ringSum = 0.0f;
                    for (int n = 0; n < ringSizes[ring]; ++n) {
                        int nx = x + rings[ring][n].dx;
                        int ny = y + rings[ring][n].dy;
                        // Clamp to image bounds (mirror would be better
                        // but clamp avoids edge-case branch cost).
                        nx = std::clamp(nx, 0, width  - 1);
                        ny = std::clamp(ny, 0, height - 1);
                        ringSum += noiseSample(nx, ny, 0u);
                    }
                    arSum += coeff * (ringSum / static_cast<float>(ringSizes[ring]));
                }

                monoNoise = arSum * arNorm;
            }

            for (int c = 0; c < colorChannels; ++c) {
                // Per-channel noise (with channel-specific hash offset)
                float center = noiseSample(x, y, kChOff[c]);
                float arSum = center * prof.ar_sigma;

                for (int ring = 0; ring < kMaxAROrder; ++ring) {
                    float coeff = prof.ar_coefficients[ring];
                    if (std::abs(coeff) < 1e-6f) continue;

                    float ringSum = 0.0f;
                    for (int n = 0; n < ringSizes[ring]; ++n) {
                        int nx = std::clamp(x + rings[ring][n].dx, 0, width  - 1);
                        int ny = std::clamp(y + rings[ring][n].dy, 0, height - 1);
                        ringSum += noiseSample(nx, ny, kChOff[c]);
                    }
                    arSum += coeff * (ringSum / static_cast<float>(ringSizes[ring]));
                }

                float perChannelNoise = arSum * arNorm;

                // ── Chroma micro-contrast blend ──────────────────────────
                // 0.0 = monochromatic, 1.0 = fully decorrelated
                rawNoise[c] = monoNoise
                    + prof.chroma_micro_contrast * (perChannelNoise - monoNoise);
            }

            // ── Spectral matrix application ──────────────────────────────
            // Row-major 3×3: [R→R, G→R, B→R, R→G, G→G, B→G, R→B, G→B, B→B]
            float grainRGB[3];
            if (colorChannels == 3) {
                const float* m = prof.spectral_matrix;
                grainRGB[0] = m[0]*rawNoise[0] + m[1]*rawNoise[1] + m[2]*rawNoise[2];
                grainRGB[1] = m[3]*rawNoise[0] + m[4]*rawNoise[1] + m[5]*rawNoise[2];
                grainRGB[2] = m[6]*rawNoise[0] + m[7]*rawNoise[1] + m[8]*rawNoise[2];
            } else {
                grainRGB[0] = rawNoise[0];
            }

            // ── Additive composite ───────────────────────────────────────
            for (int c = 0; c < colorChannels; ++c)
                dst[idx + c] = src[idx + c] + grainRGB[c] * amplitudeBase * tonalWeight;

            // ── Alpha passthrough ────────────────────────────────────────
            if (nComponents == 4)
                dst[idx + 3] = src[idx + 3];
        }
    }

    return kOfxStatOK;
}

} // namespace MasterFilm
