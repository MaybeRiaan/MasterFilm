// src/processors/ToneProcessor.h
// Pass 1: Per-channel H&D characteristic curve, log-domain pipeline.
//
// Signal flow per pixel (all operations stay in log/stop space):
//
//   encoded input
//     → toLinear()                  forward CST (log → scene linear)
//     → log2(lin / 0.18)            scene linear → stops relative to middle grey
//     → sigmoid H&D curve           stops → density (per-channel)
//     → filmColor blend             lerp R/B density toward G
//     → -densityDelta * log2(10)    density → stops out  (Beer-Lambert T = 10^-D)
//     → fromLinear(0.18 * 2^stops)  stops → scene linear → re-encode (exact CST)
//     → encoded output
//
// EXIT RAMP — BEER-LAMBERT TRANSMISSION MODEL
// ─────────────────────────────────────────────────────────────────────────────
// The positive image density D determines film transmission: T = 10^-D.
// Normalising against middle-grey density dMid:
//   T_rel = 10^-(D - dMid)  = 10^(dMid - D)
// Converting to scene-linear stops (output):
//   stops_out = log2(T_rel) = -(D - dMid) * log2(10) = -densityDelta * 3.322
// Then reconstructing the scene-linear value and re-encoding via CST:
//   lin_out  = 0.18 * 2^stops_out
//   code_out = fromLinear(lin_out, mode)
//
// KEY DIFFERENCE FROM THE PREVIOUS -(densityDelta / gamma) APPROACH:
//   - The old approach used 1/gamma as the scale, which varies per channel
//     (red=5.0, green=4.0, blue=3.33). This amplified per-channel differences,
//     causing green to spike above blue in highlights.
//   - log2(10) ≈ 3.322 is the same for all channels. Colour character now
//     comes from density differences alone (the forward curve shape), not from
//     the gamma divisor appearing a second time in the exit ramp.
//   - The fromLinear call is exact for both ACEScct and DWG — no codeMidGrey
//     or unitsPerStop approximations needed.
//
// SIGMOID CURVE MODEL
// ─────────────────────────────────────────────────────────────────────────────
// D = dMin + (dMax - dMin) / (1 + exp(-k * (stops - x0)))
//
//   k  = 4 * gamma / (dMax - dMin)   — derived from datasheet gamma
//   x0 = inflection point in stops   = (toeEnd + shoulder) / 2
//
// Replaces the piecewise toe/straight/shoulder model. No junction
// approximations, smooth curve everywhere, no 0.3f factor.
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
        static constexpr int   kLUTSize   = 4096;
        static constexpr float kStopsMin  = -8.0f;
        static constexpr float kStopsMax  =  9.0f;
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

        // Exit ramp: density → output code value via exact Beer-Lambert transmission.
        // density_delta = density - density_at_middle_grey
        // stops_out     = -density_delta * log2(10)   [T = 10^-D transmission model]
        // lin_out       = 0.18 * 2^stops_out
        // code_out      = fromLinear(lin_out, mode)   [exact, no linear approximation]
        static float densityToCode(float density, float dMid, ColorSpaceMode mode);
    };

} // namespace MasterFilm
