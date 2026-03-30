// src/processors/ToneProcessor.h
// Pass 1a: H&D curve evaluation — black/white point, toe, shoulder, mid-gamma.
// Baked into a 1D LUT for GPU efficiency; CPU path evaluates directly.
#pragma once
#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"
#include <array>

namespace MasterFilm {

class ToneProcessor {
public:
    explicit ToneProcessor(const ToneParams& params) : mParams(params) { rebuildLUT(); }

    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg) const;

    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents) const;

    void setParams(const ToneParams& p) { mParams = p; rebuildLUT(); }

private:
    ToneParams mParams;

    static constexpr int kLUTSize = 1024;
    std::array<float, kLUTSize> mLUT;

    void rebuildLUT();
    float evaluateCurve(float x) const;
};

} // namespace MasterFilm
