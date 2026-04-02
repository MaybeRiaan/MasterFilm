// src/processors/ToneProcessor.h
// Pass 1: Per-channel H&D characteristic curve with scan encoding.
//
// Physical signal flow per pixel:
//   encoded input → [CST → scene linear]
//                 → log exposure (log2 relative to middle grey)
//                 → H&D curve per channel (log exposure → density)
//                 → film color blend (lerp R/B density toward G)
//                 → scan encode (density → working space code value)
//                 → output
//
// SCAN ENCODING — DUAL ANCHOR, SINGLE SCALE
// ─────────────────────────────────────────────────────────────────────────────
// Two points on the GREEN channel's straight line define the scale:
//
//   density at 0 stops      →  codeMidGrey
//   density at shoulder     →  codeHighlight
//
//   scale = (codeHighlight - codeMidGrey) / (dHighG - dMidG)
//
// This single scale applies to ALL channels. Per-channel colour comes
// from each channel producing DIFFERENT density values at the same
// exposure — not from different encoding scales. This matches a real
// scanner, which applies one transfer function regardless of which
// emulsion layer it's reading.
//
// Each channel anchors its own dMid to codeMidGrey so that a neutral
// grey card produces neutral output. Away from grey, the channels
// diverge because their gammas, toe onsets, and shoulder onsets differ.
// That divergence IS the film's colour signature.
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
        static constexpr int   kLUTSize = 1024;
        static constexpr float kStopsMin = -8.0f;
        static constexpr float kStopsMax =  9.0f;
        static constexpr float kStopsRange = kStopsMax - kStopsMin;

        // Three LUTs — one per channel, indexed by log2 stops
        std::array<float, kLUTSize> mLUT_R;
        std::array<float, kLUTSize> mLUT_G;
        std::array<float, kLUTSize> mLUT_B;

        // Per-channel density at middle grey (0 stops)
        float mDMidR;
        float mDMidG;
        float mDMidB;

        // Green channel density at shoulder onset (for scale derivation)
        float mDHighG;

        void  rebuildLUTs();
        float evaluateCurve(float logExposure, const ChannelCurve& curve) const;
        float sampleLUT(float logExposure, const std::array<float, kLUTSize>& lut) const;

        // Scan encoding
        static float scanEncode(float density, float dMid,
                                float codeMidGrey, float scale);

        static float getCodeMidGrey(ColorSpaceMode mode);
        static float getCodeHighlight(ColorSpaceMode mode);
    };

} // namespace MasterFilm
