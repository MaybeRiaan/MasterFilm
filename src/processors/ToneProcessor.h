// src/processors/ToneProcessor.h
// Pass 1: H&D tone curve operating in scene linear light.
//
// Signal flow per pixel:
//   encoded input → [CST → scene linear] → LUT → [inverse CST → encoded] → output
//
// The curve has three regions defined by absolute scene linear input values:
//   [0, toeIn]               — smoothstep from 0 to toeOut
//   [toeIn, shoulderIn]      — linear region with midGamma, toeOut to shoulderOut
//   [shoulderIn, whitePoint] — smoothstep from shoulderOut to 1.0
//   [whitePoint, ∞]          — clamped to 1.0
//
// Input boundaries (toeIn, shoulderIn, whitePoint) are scene linear —
// directly traceable to published H&D sensitometric data.
// Output boundaries (toeOut, shoulderOut) are normalised [0,1] perceptual
// targets — authored to produce the correct visual result.
//
// This separation means physical accuracy and perceptual correctness
// are controlled independently.
//
// NOTE — Option B (future): replace output targets with density-to-scan
// pipeline using print film data (e.g. Kodak Vision Premier 2383).
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

        OfxStatus processCPU(const float* src, float* dst,
            int width, int height,
            int nComponents,
            ColorSpaceMode mode) const;

        OfxStatus processGPU(OfxImageEffectHandle effect,
            OfxPropertySetHandle srcImg,
            OfxPropertySetHandle dstImg) const;

    private:
        ToneParams mParams;

        static constexpr int   kLUTSize = 1024;
        static constexpr float kLinearMax = 16.0f;  // LUT ceiling — above whitePoint

        std::array<float, kLUTSize> mLUT;

        void  rebuildLUT();
        float evaluateCurve(float x) const;
        inline float sampleLUT(float linearVal) const;
    };

} // namespace MasterFilm