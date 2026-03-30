// src/processors/GrainProcessor.h
// Pass 4: procedural grain synthesis.
// Model: per-zone weighted noise, PSD-shaped via parametric filter.
// GPU path: GLSL / Metal. CPU fallback for testing.
#pragma once

#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"

namespace MasterFilm {

class GrainProcessor {
public:
    explicit GrainProcessor(const GrainParams& params) : mParams(params) {}

    // GPU dispatch — selects GLSL or Metal based on compile flags & host caps
    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg,
                         int renderSeed) const;

    // CPU fallback (float32 only)
    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents,
                         int renderSeed) const;

    void setParams(const GrainParams& p) { mParams = p; }
    const GrainParams& params() const    { return mParams; }

private:
    GrainParams mParams;

    // Internal helpers
    // Converts slider 'size' [0,1] → σ in pixels (PSD model parameter)
    static float sizeToSigma(float size, float iso);
    // Zone weight at given luminance [0,1]
    float zoneWeight(float luma) const;
};

} // namespace MasterFilm
