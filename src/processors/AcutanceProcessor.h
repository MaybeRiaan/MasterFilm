// src/processors/AcutanceProcessor.h
// Pass 5: edge emphasis / sharpness character.
// Three modes: Soft (gentle rolloff), Natural (balanced), Enhanced (MTF > 1.0, Kostinsky).
#pragma once
#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"

namespace MasterFilm {

class AcutanceProcessor {
public:
    explicit AcutanceProcessor(const AcutanceParams& params) : mParams(params) {}

    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg) const;

    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents) const;

    void setParams(const AcutanceParams& p) { mParams = p; }

private:
    AcutanceParams mParams;
};

} // namespace MasterFilm
