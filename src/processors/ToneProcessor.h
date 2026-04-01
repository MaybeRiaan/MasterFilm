// src/processors/ToneProcessor.h
// Pass 1: H&D tone curve operating in scene linear light.
//
// Signal flow per pixel:
//   encoded input → [CST → scene linear] → LUT → [inverse CST → encoded] → output
//
// The LUT covers 0 to kLinearMax (16.0 linear) so highlights above diffuse
// white (1.0) are handled correctly — this is where film's shoulder lives.
//
// Color space conversion uses ColorSpaceTransform.h — analytical, no alloc.
// Alpha is always passed through unchanged.
#pragma once

#include "../presets/FilmPreset.h"
#include "ColorSpaceTransform.h"
#include "ofxImageEffect.h"
#include <array>

namespace MasterFilm {

    class ToneProcessor {
    public:
        explicit ToneProcessor(const ToneParams& params) : mParams(params) { rebuildLUT(); }

        void setParams(const ToneParams& p) { mParams = p; rebuildLUT(); }

        // CPU processing — one row at a time from onRender (height=1).
        // Applies forward CST, tone LUT, inverse CST per pixel.
        // Alpha (channel 3) passes through unchanged.
        OfxStatus processCPU(const float* src, float* dst,
            int width, int height,
            int nComponents,
            ColorSpaceMode mode) const;

        // GPU stub — not yet dispatched.
        OfxStatus processGPU(OfxImageEffectHandle effect,
            OfxPropertySetHandle srcImg,
            OfxPropertySetHandle dstImg) const;

    private:
        ToneParams mParams;

        // LUT covers scene linear 0 → kLinearMax.
        // 1024 entries gives interpolation error well below float precision limits.
        static constexpr int   kLUTSize = 1024;
        static constexpr float kLinearMax = 16.0f;  // ~+6.5 stops above diffuse white

        std::array<float, kLUTSize> mLUT;

        void  rebuildLUT();
        float evaluateCurve(float x) const;         // x in [0, kLinearMax]
        inline float sampleLUT(float linearVal) const;
    };

} // namespace MasterFilm