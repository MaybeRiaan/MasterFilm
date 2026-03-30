// src/processors/HalationProcessor.h
#pragma once
#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"

namespace MasterFilm {

// Pass 2 & 3: two-Gaussian horizontal + vertical blur with spectral weighting.
// Rendered in two sub-passes to keep GPU memory bandwidth manageable.
class HalationProcessor {
public:
    explicit HalationProcessor(const HalationParams& params) : mParams(params) {}

    // Pass 2: horizontal blur into intermediate buffer
    OfxStatus processHorizontalGPU(OfxImageEffectHandle effect,
                                   OfxPropertySetHandle srcImg,
                                   OfxPropertySetHandle dstImg) const;

    // Pass 3: vertical blur + composite back onto source
    OfxStatus processVerticalGPU(OfxImageEffectHandle effect,
                                 OfxPropertySetHandle hBlurImg,
                                 OfxPropertySetHandle srcImg,
                                 OfxPropertySetHandle dstImg) const;

    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents) const;

    void setParams(const HalationParams& p) { mParams = p; }

private:
    HalationParams mParams;

    // Radius [0,1] → pixel radius based on image height
    float radiusInPixels(int imageHeight) const;
};

} // namespace MasterFilm
