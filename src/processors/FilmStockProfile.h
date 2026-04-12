// src/processors/FilmStockProfile.h
// GPU-transferable data model for the stochastic film grain synthesis engine.
//
// This struct parameterizes a complete film stock's grain characteristics
// for transfer to GPU uniform buffers (GLSL std140 / OpenCL __constant /
// CUDA constant memory).  Every field is documented with its physical
// origin, expected range, and representative per-stock values.
//
// DESIGN NOTES
// ---------------------------------------------------------------------------
// - POD / standard-layout:  no virtuals, no heap, no ctors with side-effects.
//   Safe to memcpy into a mapped UBO or pass as a kernel argument.
// - The tonal_lut replaces any hardcoded luminance-variance function.
//   It is populated on the host from spline control points or datasheet
//   curves, allowing accurate simulation of Kodak DLT shadow-smoothing
//   vs. the elevated shadow noise of older reversal stocks.
// - The AR coefficients define spatial autocorrelation (grain clumping).
//   On the GPU they are applied as a symmetric FIR convolution over
//   hash-regenerated neighbor noise (no feedback dependency).
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace MasterFilm {

// ── Grain morphology enum ────────────────────────────────────────────────────
// Selects the Boolean model's spatial kernel for individual grain particles.
//
//   Cubic  : Gaussian radius distribution.
//            Classic cubic silver halide crystals (Tri-X, HP5, Double-X).
//            Produces symmetric, normally-distributed grain amplitudes.
//
//   TGrain : Log-normal radius distribution.
//            Kodak's tabular grain technology (T-Max, Vision3, Portra).
//            Flatter coverage, positively-skewed amplitude distribution —
//            fewer extreme outliers, tighter perceived grain.
//
enum class GrainMorphology : int32_t {
    Cubic  = 0,
    TGrain = 1
};

// Maximum auto-regressive filter order.
// 4 taps covers the published grain PSD models
// (Newson et al. 2017, Yan et al. 2016).
static constexpr int kMaxAROrder   = 4;

// Number of entries in the tonal distribution LUT.
// 64 gives sub-2% luminance quantisation across [0,1] —
// smooth enough for perceptually invisible stepping when
// interpolated linearly in the shader.
static constexpr int kTonalLUTSize = 64;

// ── FilmStockProfile ─────────────────────────────────────────────────────────
//
// Complete parameterisation of a film stock's grain characteristics.
// One instance is uploaded per render call to the GPU kernel.
//
struct FilmStockProfile {

    // ── Global amplitude ──────────────────────────────────────────────────
    // RMS granularity from published datasheet (sigma_D x 1000).
    // This is the primary amplitude scalar — all other modulations
    // (tonal LUT, AR sigma, morphology) are relative to this.
    //
    //   Kodak Vision3 500T : 12.0      Ilford HP5 Plus 400 : 15.0
    //   Kodak Vision3 50D  :  5.0      Kodak Tri-X 400     : 16.0
    //   Kodak T-Max 100    :  6.0      Kodak Double-X      : 14.0
    //   Fuji Ektachrome    : 10.0      Fuji Provia 100F    :  8.0
    float rms_granularity = 12.0f;

    // ── Morphology ────────────────────────────────────────────────────────
    // Selects the noise amplitude distribution shape.
    //   0 = Cubic  (Gaussian)   — symmetric bell curve
    //   1 = TGrain (log-normal) — positively skewed, tighter variance
    // Stored as int32_t for GPU compatibility (no enum in GLSL).
    int32_t morphology_type = static_cast<int32_t>(GrainMorphology::Cubic);

    // ── Auto-Regressive spatial correlation ───────────────────────────────
    // Coefficients for the AR(p) agglomeration model.  On the GPU these
    // drive a symmetric FIR convolution over regenerated neighbor noise:
    //
    //   ar_coefficients[0] : weight for ring-1 (4-connected N/S/E/W)
    //   ar_coefficients[1] : weight for ring-1 diagonals (NE/NW/SE/SW)
    //   ar_coefficients[2] : weight for ring-2 (2-pixel N/S/E/W)
    //   ar_coefficients[3] : weight for ring-2 diagonals
    //
    // Higher values → more spatial correlation → larger clumps.
    // All zero → spatially uncorrelated (white) noise.
    //
    //   Vision3 500T : {0.42, 0.18, 0.06, 0.00}  moderate clumping
    //   Tri-X 400    : {0.55, 0.25, 0.10, 0.03}  heavy clumping
    //   T-Max 100    : {0.30, 0.10, 0.00, 0.00}  tight, fine grain
    //   Ektachrome   : {0.35, 0.12, 0.04, 0.00}  moderate, fine
    float ar_coefficients[kMaxAROrder] = {0.42f, 0.18f, 0.06f, 0.0f};

    // Innovation variance scale for the AR filter.
    // Multiplies the center-pixel (white noise) contribution.
    // Set to 1.0 to let rms_granularity control the overall amplitude.
    float ar_sigma = 1.0f;

    // ── Spectral dye layer crosstalk ──────────────────────────────────────
    // 3x3 matrix defining inter-channel grain correlation from dye layer
    // coupling.  Row-major storage:
    //   [R->R, G->R, B->R,   R->G, G->G, B->G,   R->B, G->B, B->B]
    //
    // Physical basis: in colour negative film, each dye layer
    // (cyan / magenta / yellow) has a slightly different grain structure.
    // Spectral sensitivity overlap between layers creates correlated
    // grain across channels.
    //
    // PRESETS
    //   Tungsten 500T — heavy blue-channel weighting (tungsten spectral bias):
    //     { 1.00, 0.12, 0.04,
    //       0.08, 1.00, 0.10,
    //       0.06, 0.15, 1.00 }
    //
    //   Daylight 50D — balanced crosstalk:
    //     { 1.00, 0.06, 0.02,
    //       0.05, 1.00, 0.05,
    //       0.02, 0.06, 1.00 }
    //
    //   Achromatic B&W — identity (bypass):
    //     { 1.00, 0.00, 0.00,
    //       0.00, 1.00, 0.00,
    //       0.00, 0.00, 1.00 }
    float spectral_matrix[9] = {
        1.00f, 0.12f, 0.04f,
        0.08f, 1.00f, 0.10f,
        0.06f, 0.15f, 1.00f
    };

    // ── Chroma micro-contrast ─────────────────────────────────────────────
    // Controls colour jitter / saturation of grain clouds.
    //   0.0 = perfectly monochromatic (all channels receive identical noise)
    //   1.0 = fully decorrelated per-channel noise (maximum colour grain)
    //
    // Physical basis: dye cloud boundaries do not align perfectly across
    // emulsion layers, creating localised chroma variations.
    //
    //   B&W stocks     : 0.00   (monochromatic by definition)
    //   Vision3 500T   : 0.35   (moderate — DLT suppresses chroma noise)
    //   Ektachrome     : 0.55   (reversal amplifies dye misalignment)
    //   Double-X       : 0.00   (B&W)
    float chroma_micro_contrast = 0.35f;

    // ── Tonal distribution LUT (DLT simulation) ──────────────────────────
    // 1D lookup table mapping normalised luminance [0,1] to grain
    // amplitude multiplier [0, inf).  The shader linearly interpolates
    // between entries.
    //
    // This replaces ANY hardcoded luminance-variance function and is the
    // key mechanism for simulating:
    //
    //   DLT negative (Vision3)  : suppressed shadows, peak at mid,
    //                             gentle highlight rolloff
    //   Reversal (Ektachrome)   : elevated shadow noise, sharp mid peak,
    //                             steep highlight suppression
    //   Classic B&W (Tri-X)     : heavy shadows, broad mid plateau,
    //                             moderate highlight rolloff
    //
    // Populated on the host from spline control points or measured curves.
    // Default: flat response (1.0 everywhere = uniform grain).
    float tonal_lut[kTonalLUTSize] = {
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f
    };

    // ── Execution context ─────────────────────────────────────────────────
    // Deterministic temporal PRNG.
    // Per-frame seed = Hash(global_seed + frame_index).
    // Guarantees identical grain for the same (seed, frame) pair across
    // cache re-renders and multi-machine distributed renders.
    uint32_t global_seed = 0x5EED0042u;
    int32_t  frame_index = 0;

    // ── Grain size / ISO reference ────────────────────────────────────────
    // grain_size [0,1] maps to [0.3, 3.0] x sigma_base.
    // sigma_base = 0.35 * sqrt(ISO / 100) pixels.
    float grain_size = 0.48f;
    float iso        = 500.0f;

    // ── Sliding window overlap (compute shader path) ──────────────────────
    // Width of the overlap halo in pixels for tiled GPU dispatch.
    // Must be >= effective AR filter radius to prevent seam artifacts.
    //
    // In the GLSL fragment shader path this is unused — every fragment
    // has full-image texture access and regenerates neighbor noise via
    // the hash function, so there are no tile boundaries.
    //
    // In an OpenCL/CUDA compute kernel, workgroups would tile the image
    // with (tile_size + 2 * overlap_pixels) shared local memory.
    int32_t overlap_pixels = 16;

    // Padding — keeps the struct size a multiple of 16 bytes for
    // std140 / __attribute__((aligned(16))) compatibility.
    int32_t _pad0 = 0;
};

// ── Tonal LUT generators ─────────────────────────────────────────────────────
// Host-side utilities to populate FilmStockProfile::tonal_lut from
// physically-motivated curve models.

// DLT-style curve (modern negative stocks with Dye Layering Technology).
// Suppressed shadow noise, peak at mid-tones, gentle highlight rolloff.
//   shadowFloor     : minimum multiplier in deep shadows [0, 1]
//   midPeak         : peak multiplier at mid-tones (typically 1.0)
//   highlightRolloff: controls highlight attenuation rate (higher = steeper)
void generateDLTTonalLUT(float* lut, int size,
                         float shadowFloor, float midPeak,
                         float highlightRolloff);

// Reversal-style curve (slide films: Ektachrome, Velvia, Provia).
// Elevated shadow noise, sharp mid peak, steep highlight suppression.
//   shadowLevel   : shadow amplitude [0, 1]
//   midPeak       : peak at mid-tones (typically 1.0)
//   highlightCutoff: luminance above which grain is strongly suppressed
void generateReversalTonalLUT(float* lut, int size,
                              float shadowLevel, float midPeak,
                              float highlightCutoff);

// Classic B&W curve (Tri-X, HP5, Double-X).
// Heavy shadow grain, broad mid plateau, moderate highlight rolloff.
//   shadowWeight : relative shadow grain strength [0, 1]
//   plateauWidth : width of the mid-tone plateau (fraction of tonal range)
void generateClassicBWTonalLUT(float* lut, int size,
                               float shadowWeight, float plateauWidth);

// Flat response — 1.0 everywhere.  Uniform grain, no tonal modulation.
void generateFlatTonalLUT(float* lut, int size);

} // namespace MasterFilm
