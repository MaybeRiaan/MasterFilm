// src/processors/ToneProcessor.h
// Pass 1: Per-channel H&D characteristic curve, log-domain pipeline.
//
// Signal flow per pixel (all operations stay in log/stop space):
//
//   encoded input
//     → toLinear()                  forward CST (log → scene linear)
//     → log2(lin / 0.18)            scene linear → stops relative to middle grey
//     → sigmoid H&D curve           stops → density (negative film model)
//     → filmColor blend             lerp R/B density toward G
//     → exit ramp with print gamma  density → stops out → scene linear → re-encode
//     → encoded output
//
// EXIT RAMP — BEER-LAMBERT + PRINT STOCK GAMMA
// ─────────────────────────────────────────────────────────────────────────────
// The negative film density D determines transmission: T = 10^-D.
// Normalising against middle-grey density dMid:
//   densityDelta = D - dMid
//
// Without a print stock, the raw density delta maps to stops via:
//   stops_out = -densityDelta * log2(10)          [Beer-Lambert alone]
//
// But the negative film was never meant to be viewed directly. In the
// photochemical chain, the negative is contact-printed onto a print stock
// (e.g. Kodak 2383) which amplifies the density differences through its
// own gamma. This print gamma is the missing contrast stage that causes
// shadow crushing and limited output range when omitted.
//
// The print stock model multiplies the density delta:
//   stops_out = -densityDelta * log2(10) * printGamma
//
// This is physically correct: the print stock's characteristic curve
// is approximately linear through its straight-line region, and its
// slope (gamma) directly scales the negative's density differences.
// The multiplier is uniform across channels — colour character still
// comes from per-channel density differences in the negative curve.
//
// Middle grey is unaffected: densityDelta = 0 at mid, so printGamma
// cancels out. The anchoring is exact regardless of the gamma value.
//
// Default printGamma = 1.8 approximates a scan-graded projection
// through 2383 print stock. Phase 2 will replace this with the actual
// 2383 characteristic curve for a proper composable print stage.
//
// SIGMOID CURVE MODEL
// ─────────────────────────────────────────────────────────────────────────────
// D = dMax - (dMax - dMin) / (1 + exp(-k * (stops - x0)))
//
//   k  = 4 * gamma / (dMax - dMin)   — derived from datasheet gamma
//   x0 = inflection point in stops   = (toeEnd + shoulder) / 2
//
// FILM COLOR CONTROL
// ─────────────────────────────────────────────────────────────────────────────
//   0.0 — all channels use green curve (pure tone, no colour shift)
//   1.0 — each channel uses its own curve (full stock colour signature)
//  >1.0 — exaggerated colour separation (artistic override)
#pragma once

#include "../presets/FilmPreset.h"
#include "ColorSpaceTransform.h"
#include "ofxImageEffect.h"
#include <array>
#include <cmath>

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
        static constexpr float kStopsMin = -8.0f;
        static constexpr float kStopsMax = 9.0f;
        static constexpr float kStopsRange = kStopsMax - kStopsMin;

        // Physical density → stop scale factor (Beer-Lambert law: T = 10^-D)
        // d(stops_out)/d(density) = d(log2(T_rel))/d(D) = -log2(10)
        // Using this constant for all channels ensures colour character comes
        // from density differences alone, not from the gamma divisor.
        static constexpr float kLog2of10 = 3.321928f;  // log2(10)

        // Three LUTs — one per channel, indexed by log2 stops
        // Stores DENSITY values, not output code values.
        // The exit ramp is applied at runtime per-pixel so that gamma
        // scaling (LiteUI) takes effect without a LUT rebuild.
        std::array<float, kLUTSize> mLUT_R;
        std::array<float, kLUTSize> mLUT_G;
        std::array<float, kLUTSize> mLUT_B;

        // Density at middle grey (0 stops) — per-channel anchors
        float mDMidR = 0.0f;
        float mDMidG = 0.0f;
        float mDMidB = 0.0f;

        void  rebuildLUTs();

        static float evaluateCurve(float stops, const ChannelCurve& c);
        float        sampleLUT(float stops, const std::array<float, kLUTSize>& lut) const;

        // Exit ramp: density → output code value via Beer-Lambert + print gamma.
        // density_delta = density - dMid
        // stops_out     = -density_delta * log2(10) * printGamma
        // lin_out       = 0.18 * 2^stops_out
        // code_out      = fromLinear(lin_out, mode)
        //
        // printGamma amplifies density differences — models the print stock's
        // contrast contribution. Middle grey is unaffected (delta=0).
        static float densityToCode(float density, float dMid,
            float printGamma, ColorSpaceMode mode);
    };

} // namespace MasterFilm