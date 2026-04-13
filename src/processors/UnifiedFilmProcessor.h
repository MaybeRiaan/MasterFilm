// src/processors/UnifiedFilmProcessor.h
// Unified four-stage photochemical film emulation pipeline.
//
// Replaces the split ToneProcessor + ColorProcessor with a single
// sequential pipeline that matches the physical photochemical chain:
//
//   Stage 1 — Exposure:   encoded input → scene linear → log2 stops
//   Stage 2 — Negative:   stops → density (per-channel H&D sigmoid LUT)
//                          + filmColor blend (R/B toward G)
//                          + inter-layer coupling (density-space matrix)
//   Stage 3 — Timing:     printer lights as density offsets
//   Stage 4 — Print:      density delta → print LUT → output code value
//
// PRINT LUT (PERFORMANCE)
// ─────────────────────────────────────────────────────────────────────────────
// The entire exit ramp (Beer-Lambert + print curve + re-encode) is baked
// into a 1D LUT indexed by density delta. This replaces per-pixel pow(),
// exp(), and log() calls with a single interpolated table lookup per
// channel — roughly 5-10× faster than the analytical path.
//
// The print LUT supports two modes:
//   LINEAR:  stopsOut = -delta × log₂(10) × printGamma  (original model)
//            Single shared LUT — printGamma uniform across channels.
//   2383:    Per-channel H&D sigmoid curves from Kodak 2383 datasheet.
//            Three separate LUTs — each dye layer (cyan, magenta, yellow)
//            has its own dMin, dMax, gamma, x0 producing natural colour
//            separation. Parameters derived from published sensitometric
//            data (F002_1254AC, ECP-2D, Status A densitometry).
//
// The LUT is rebuilt when parameters or ColorSpaceMode change.
//
// DENSITY-SPACE COUPLING
// ─────────────────────────────────────────────────────────────────────────────
// The inter-layer coupling matrix (from ColorParams) is applied in
// density space between stage 2 and stage 3. Physically correct.
//
// GPU READINESS
// ─────────────────────────────────────────────────────────────────────────────
// processGPU() returns kOfxStatReplyDefault (stub). CPU path structured
// for GLSL transliteration: density LUTs as texture1D, print LUT as
// texture1D, matrices as mat3 uniforms, printer lights as vec3 uniform.
#pragma once

#include "../presets/FilmPreset.h"
#include "ColorSpaceTransform.h"
#include "ofxImageEffect.h"
#include <array>
#include <cmath>

namespace MasterFilm {

class UnifiedFilmProcessor {
public:
    UnifiedFilmProcessor(const ToneParams& tone,
                         const TimingParams& timing,
                         const PrintParams& print,
                         const ColorParams& color);

    void setParams(const ToneParams& tone,
                   const TimingParams& timing,
                   const PrintParams& print,
                   const ColorParams& color);

    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height,
                         int nComponents,
                         ColorSpaceMode mode) const;

    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg) const;

private:
    ToneParams   mTone;
    TimingParams mTiming;
    PrintParams  mPrint;
    ColorParams  mColor;

    // ── Negative LUT parameters — log2 stops domain ──────────────────────────
    static constexpr int   kLUTSize    = 4096;
    static constexpr float kStopsMin   = -8.0f;
    static constexpr float kStopsMax   =  9.0f;
    static constexpr float kStopsRange = kStopsMax - kStopsMin;

    // Beer-Lambert: d(stops)/d(density) = -log2(10)
    static constexpr float kLog2of10 = 3.321928f;

    // ── Per-channel negative density LUTs ────────────────────────────────────
    // Indexed by log2 stops, stores density values (no exit ramp baked in).
    std::array<float, kLUTSize> mLUT_R;
    std::array<float, kLUTSize> mLUT_G;
    std::array<float, kLUTSize> mLUT_B;

    // Density at 0 stops (middle grey) — per-channel anchors (pre-coupling)
    float mDMidR = 0.0f;
    float mDMidG = 0.0f;
    float mDMidB = 0.0f;

    // Coupled dMid values — after applying the coupling matrix to the
    // raw dMid triplet. Used in the exit ramp so that middle grey remains
    // exactly anchored (densityDelta = 0) after coupling.
    float mCoupledDMidR = 0.0f;
    float mCoupledDMidG = 0.0f;
    float mCoupledDMidB = 0.0f;

    // ── Print LUTs — density delta → output code value ─────────────────────
    // Bakes the entire exit ramp: Beer-Lambert + print curve + re-encode.
    //
    // In LINEAR mode: one shared LUT (printGamma uniform across channels).
    // In 2383 mode:   three per-channel LUTs — each channel's print sigmoid
    //                 has its own dMin, dMax, gamma, x0 from 2383 datasheet.
    //                 This produces natural colour separation from the print
    //                 stock's dye-layer differences.
    //
    // Rebuilt when print params or ColorSpaceMode change.
    static constexpr int   kPrintLUTSize  = 4096;
    static constexpr float kDeltaMin      = -3.0f;   // density delta range
    static constexpr float kDeltaMax      =  3.0f;
    static constexpr float kDeltaRange    = kDeltaMax - kDeltaMin;

    mutable std::array<float, kPrintLUTSize> mPrintLUT_R;
    mutable std::array<float, kPrintLUTSize> mPrintLUT_G;
    mutable std::array<float, kPrintLUTSize> mPrintLUT_B;
    mutable ColorSpaceMode mPrintLUTMode = ColorSpaceMode::ACEScct;
    mutable bool mPrintLUTValid = false;

    // ── Printer light stop offsets (precomputed from Kodak scale) ─────────────
    float mTimingOffsetR = 0.0f;
    float mTimingOffsetG = 0.0f;
    float mTimingOffsetB = 0.0f;

    // ── Internal methods ─────────────────────────────────────────────────────

    void rebuildLUTs();
    void rebuildTimingOffsets();
    void rebuildPrintLUT(ColorSpaceMode mode) const;

    // Sigmoid H&D curve: positive characteristic (fused negative + inversion)
    static float evaluateCurve(float stops, const ChannelCurve& c);

    // LUT samplers with linear interpolation
    float sampleLUT(float stops, const std::array<float, kLUTSize>& lut) const;
    float samplePrintLUT(float densityDelta,
                         const std::array<float, kPrintLUTSize>& lut) const;

    // Coupling matrix multiply in density space
    void applyCouplingDensity(float& dR, float& dG, float& dB) const;

    // ── Zone colour (artistic post-process) ──────────────────────────────────
    void applyZoneColor(float& r, float& g, float& b,
                        float luma, ColorSpaceMode mode) const;

    static void rgbToHsl(float r, float g, float b,
                         float& h, float& s, float& l);
    static void hslToRgb(float h, float s, float l,
                         float& r, float& g, float& b);

    static float shadowBlend(float luma);
    static float midBlend(float luma);
    static float highlightBlend(float luma);

    // Luminance coefficients for the current colour space
    static void getLumaCoeffs(ColorSpaceMode mode,
                              float& wR, float& wG, float& wB);
};

} // namespace MasterFilm
