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
//     → -(density_delta / gamma)    density → stops out  (log-domain exit ramp)
//     → fromLinear(0.18 * 2^stops)  stops → scene linear → re-encode
//     → encoded output
//
// EXIT RAMP — WHY LOG DOMAIN
// ─────────────────────────────────────────────────────────────────────────────
// Previous approaches tried to convert density back to scene-linear radiance
// via fromLinear(). This fails because density is not radiometric — it is an
// optical property. The unit mismatch produced clipping and distortion.
//
// Instead: express the density change as a stop delta from middle grey,
// then convert that stop delta back to a code value. The gamma parameter
// (density/stop) is the bridge — dividing density_delta by gamma gives stops.
// This keeps the entire pipeline in log/stop space with no unit crossing.
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

        // ACEScct units per one stop — used for the log-domain exit ramp.
        // Derived from the ACEScct log segment: d(cct)/d(stops) = log2(e) * 17.52 / (ln(2) * ... )
        // Simplified: one stop = 1/17.52 in ACEScct log space.
        // Validated in standalone test: middle grey ±1 stop = ±0.0816 ACEScct.
        static constexpr float kACESPerStop = 1.0f / 17.52f;  // ≈ 0.05709

        // DaVinci Intermediate units per stop at middle grey (0.5 encoded).
        // DWG log segment: cct = DI_C * (log2(lin + DI_A) + DI_B)
        // d(cct)/d(log2(lin)) = DI_C = 0.07329248
        static constexpr float kDWGPerStop  = 0.07329248f;

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

        // Log-domain exit ramp: density → output code value
        // density_delta = density - density_at_middle_grey
        // stops_out     = -(density_delta / gamma)
        // code_out      = codeMidGrey + stops_out * unitsPerStop
        static float densityToCode(float density, float dMid,
                                   float gamma,
                                   float codeMidGrey,
                                   float unitsPerStop);

        static float getCodeMidGrey(ColorSpaceMode mode);
        static float getUnitsPerStop(ColorSpaceMode mode);
    };

} // namespace MasterFilm
