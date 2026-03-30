// src/processors/ToneProcessor.h
// Pass 1: H&D tone curve — black/white point, toe, shoulder, mid-gamma.
// Baked into a 1024-entry 1D LUT at construction time.
// CPU path walks pixels with a per-channel unrolled loop (no modulo).
// GPU path (stub) will upload the LUT as a 1D texture.
#pragma once

#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"
#include <array>

namespace MasterFilm {

    class ToneProcessor {
    public:
        explicit ToneProcessor(const ToneParams& params) : mParams(params) { rebuildLUT(); }

        // Update parameters and rebake the LUT.
        void setParams(const ToneParams& p) { mParams = p; rebuildLUT(); }

        // CPU processing — processes width*height pixels from src into dst.
        // Called once per row from onRender (height=1), so src/dst are row pointers.
        // Alpha channel (index 3 in RGBA) is always passed through unchanged.
        OfxStatus processCPU(const float* src, float* dst,
            int width, int height, int nComponents) const;

        // GPU stub — not yet dispatched.
        OfxStatus processGPU(OfxImageEffectHandle effect,
            OfxPropertySetHandle srcImg,
            OfxPropertySetHandle dstImg) const;

    private:
        ToneParams mParams;

        static constexpr int kLUTSize = 1024;
        std::array<float, kLUTSize> mLUT;

        void  rebuildLUT();
        float evaluateCurve(float x) const;

        // Bilinear LUT lookup — inlined for hot-path performance.
        inline float sampleLUT(float v) const;
    };

} // namespace MasterFilm