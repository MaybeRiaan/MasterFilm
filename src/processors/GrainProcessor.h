// src/processors/GrainProcessor.h
// Pass 4: stochastic film grain synthesis.
//
// Model: Boolean grain model with parameterised morphology (T-Grain /
// Cubic), AR-driven spatial correlation, spectral dye layer coupling,
// and LUT-driven tonal distribution (DLT simulation).
//
// GPU path : GLSL fragment shader (shaders/glsl/grain.glsl).
// CPU path : full fallback implementing the identical algorithm.
#pragma once

#include "../presets/FilmPreset.h"
#include "FilmStockProfile.h"
#include "ofxImageEffect.h"

namespace MasterFilm {

class GrainProcessor {
public:
    // ── Construction ──────────────────────────────────────────────────────
    // Preferred: construct directly from a FilmStockProfile.
    explicit GrainProcessor(const FilmStockProfile& profile)
        : mProfile(profile) {}

    // Legacy: construct from the old GrainParams (auto-converts to profile).
    explicit GrainProcessor(const GrainParams& params)
        : mProfile(buildDefaultProfile(params)) {}

    // ── GPU dispatch ──────────────────────────────────────────────────────
    // Renders grain via the GLSL / Metal shader.
    // The caller must have an active GL/Metal context and must call
    // glUseProgram() on the grain shader program before this call.
    // renderSeed is kept for API compat; the profile's global_seed +
    // frame_index are the authoritative temporal seed.
    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg,
                         int renderSeed) const;

    // ── CPU fallback ──────────────────────────────────────────────────────
    // Full stochastic grain synthesis on float32 buffers.
    // Implements the same algorithm as the GPU shader:
    //   1. Deterministic PRNG seeding from profile.global_seed + frame_index
    //   2. Per-pixel white noise generation (hash-based)
    //   3. Morphology shaping (Gaussian or log-normal distribution)
    //   4. AR spatial correlation via neighbour noise regeneration
    //   5. Chroma micro-contrast decorrelation
    //   6. Spectral matrix application (dye layer crosstalk)
    //   7. Tonal LUT amplitude modulation
    //   8. Additive composite in linear-light space
    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents,
                         int renderSeed) const;

    // ── Profile access ────────────────────────────────────────────────────
    void setProfile(const FilmStockProfile& p)   { mProfile = p; }
    const FilmStockProfile& profile() const      { return mProfile; }

    // Legacy API shim — converts and stores as profile internally.
    void setParams(const GrainParams& p) { mProfile = buildDefaultProfile(p); }

private:
    FilmStockProfile mProfile;

    // ── Internal helpers ──────────────────────────────────────────────────
    // Converts grain_size [0,1] + ISO → sigma in pixels (PSD scale).
    static float sizeToSigma(float size, float iso);

    // Wang hash — combinational, deterministic, no state.
    static uint32_t wangHash(uint32_t seed);

    // Map hash output to float in [-1, 1].
    static float hashToFloat(uint32_t h);

    // Combine pixel coordinates + seed into a single hash.
    static uint32_t pixelHash(int x, int y, uint32_t seed);

    // Sample the tonal LUT with linear interpolation.
    static float sampleTonalLUT(const float* lut, int lutSize, float luma);

    // Apply morphology shaping to a Gaussian sample.
    static float shapeMorphology(float gaussianNoise, int morphType);
};

} // namespace MasterFilm
