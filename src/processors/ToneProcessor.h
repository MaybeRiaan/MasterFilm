// src/processors/ToneProcessor.h
// Pass 1: Per-channel H&D characteristic curve as a transfer function.
//
// Physical signal flow per pixel:
//   encoded input → [CST → scene linear]
//                 → log exposure (log2 relative to middle grey)
//                 → H&D curve per channel (stops → density)
//                 → FULL curve inverse (density → original stops)
//                 → equivalent linear (0.18 × 2^stops)
//                 → film color blend
//                 → [CST → encoded output]
//
// FULL CURVE INVERSE
// ─────────────────────────────────────────────────────────────────────────────
// The previous approach inverted only the straight-line region, which caused
// lifted blacks because the toe's compression was not correctly unwound.
//
// The full inverse inverts each region (toe, straight, shoulder) separately:
//   - Straight line: stops = (density - dToeEnd) / gamma + toeEndStops
//   - Toe:           inverts the smoothstep to recover original stops
//   - Shoulder:      inverts the smoothstep to recover original stops
//
// In the straight-line region this is an identity (perfect pass-through).
// In the toe, the inverse unwinding maps base fog (dMin) back to toeStart,
// which maps to a deeply negative exposure → near-zero linear → true black.
// In the shoulder, the inverse maps dMax back to clipStops, preserving
// the full highlight rolloff.
//
// Film Color control:
//   0.0 — all channels use green curve (pure tone, no colour shift)
//   1.0 — each channel uses its own curve (full stock colour signature)
#pragma once

#include "../presets/FilmPreset.h"
#include "ColorSpaceTransform.h"
#include "ofxImageEffect.h"
#include <array>

namespace MasterFilm {

    class ToneProcessor {
    public:
        explicit ToneProcessor(const ToneParams& params) : mParams(params) { rebuildLUTs(); }

        void setParams(const ToneParams& p) { mParams = p; rebuildLUTs(); }

        OfxStatus processCPU(const float* src, float* dst,
            int width, int height,
            int nComponents,
            ColorSpaceMode mode) const;

        OfxStatus processGPU(OfxImageEffectHandle effect,
            OfxPropertySetHandle srcImg,
            OfxPropertySetHandle dstImg) const;

    private:
        ToneParams mParams;

        // LUT parameters — log2 stops domain
        static constexpr int   kLUTSize = 4096;
        static constexpr float kStopsMin = -10.0f;
        static constexpr float kStopsMax =  10.0f;
        static constexpr float kStopsRange = kStopsMax - kStopsMin;

        // Three LUTs — one per channel, indexed by log2 stops
        // Output is equivalent linear after full curve inverse
        std::array<float, kLUTSize> mLUT_R;
        std::array<float, kLUTSize> mLUT_G;
        std::array<float, kLUTSize> mLUT_B;

        void  rebuildLUTs();

        // Forward H&D curve: stops → density
        float evaluateCurve(float logExposure, const ChannelCurve& curve) const;

        // Full curve inverse: density → original stops
        // Inverts toe, straight line, and shoulder separately
        float inverseCurve(float density, const ChannelCurve& curve) const;

        // Combined transfer: stops → density → inverse → equivalent stops → linear
        float filmTransfer(float stops, const ChannelCurve& curve) const;

        float sampleLUT(float logExposure, const std::array<float, kLUTSize>& lut) const;

        // Pre-computed curve geometry per channel (for inverse)
        struct CurveGeometry {
            float dToeEnd;
            float dShoulder;
            float dShoulderClamped;
        };
        CurveGeometry mGeomR, mGeomG, mGeomB;

        static CurveGeometry computeGeometry(const ChannelCurve& c);
    };

} // namespace MasterFilm
