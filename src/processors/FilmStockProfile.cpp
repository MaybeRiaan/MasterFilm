// src/processors/FilmStockProfile.cpp
// Host-side utilities for FilmStockProfile:
//   - Tonal LUT generators (DLT, reversal, classic B&W, flat)
//   - Legacy GrainParams → FilmStockProfile bridge
#include "FilmStockProfile.h"
#include "../presets/FilmPreset.h"

#include <algorithm>
#include <cmath>

namespace MasterFilm {

// ── Tonal LUT generators ─────────────────────────────────────────────────────
//
// All generators write exactly `size` floats into `lut`.
// The index maps linearly to normalised luminance:
//   lut[0] = deep shadow (luma ≈ 0)
//   lut[size-1] = peak highlight (luma ≈ 1)
//
// Values are non-negative amplitude multipliers (0 = no grain, 1 = full).
// Values > 1 are legal and represent amplified grain (e.g. pushed processing).

void generateDLTTonalLUT(float* lut, int size,
                         float shadowFloor, float midPeak,
                         float highlightRolloff)
{
    // Model: smooth ramp from shadowFloor, peak at mid, cosine rolloff
    // in highlights.  Approximates the measured grain-vs-density curves
    // from Kodak DLT negative stocks (Vision3 family).
    //
    //   shadows    : floor + gentle ramp         (DLT suppression)
    //   mid-tones  : peak amplitude              (most visible grain)
    //   highlights : cosine rolloff to near-zero  (density saturation)
    for (int i = 0; i < size; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(size - 1);

        // Shadow ramp: smoothstep from floor to peak, centred at t ≈ 0.3
        float shadowRamp = shadowFloor + (midPeak - shadowFloor)
                         * std::min(1.0f, t / 0.35f);
        // Smoothstep shaping
        float s = std::min(t / 0.35f, 1.0f);
        s = s * s * (3.0f - 2.0f * s);
        shadowRamp = shadowFloor + (midPeak - shadowFloor) * s;

        // Highlight rolloff: cosine decay starting at t ≈ 0.55
        float highlightDecay = 1.0f;
        if (t > 0.55f) {
            float h = (t - 0.55f) / (1.0f - 0.55f);
            highlightDecay = 0.5f * (1.0f + std::cos(h * 3.14159265f));
            highlightDecay = std::pow(highlightDecay, highlightRolloff);
        }

        lut[i] = shadowRamp * highlightDecay;
    }
}

void generateReversalTonalLUT(float* lut, int size,
                              float shadowLevel, float midPeak,
                              float highlightCutoff)
{
    // Reversal stocks (Ektachrome, Velvia) have elevated shadow noise
    // because the reversal process amplifies silver clumping in shadows.
    // Sharp mid-tone peak, steep highlight suppression (dense dye layers
    // physically mask grain in highlights).
    for (int i = 0; i < size; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(size - 1);

        // Shadow plateau at elevated level, rising to mid peak
        float base = shadowLevel + (midPeak - shadowLevel)
                   * (1.0f - std::exp(-4.0f * t));

        // Steep highlight suppression — sigmoid cutoff
        float highlightMask = 1.0f;
        if (t > highlightCutoff) {
            float h = (t - highlightCutoff) / (1.0f - highlightCutoff);
            highlightMask = 1.0f - h * h;  // quadratic decay
            highlightMask = std::max(highlightMask, 0.0f);
        }

        lut[i] = base * highlightMask;
    }
}

void generateClassicBWTonalLUT(float* lut, int size,
                               float shadowWeight, float plateauWidth)
{
    // Classic B&W stocks: heavy shadow grain (large cubic crystals in
    // under-exposed regions), broad mid-tone plateau, moderate rolloff.
    // plateauWidth controls the fraction of the tonal range at peak.
    float plateauStart = 0.5f * (1.0f - plateauWidth);
    float plateauEnd   = 0.5f * (1.0f + plateauWidth);

    for (int i = 0; i < size; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(size - 1);
        float val;

        if (t < plateauStart) {
            // Shadow region — elevated grain, blending to plateau
            float s = t / plateauStart;
            val = shadowWeight + (1.0f - shadowWeight) * s;
        } else if (t <= plateauEnd) {
            // Mid-tone plateau — peak grain
            val = 1.0f;
        } else {
            // Highlight rolloff — linear decay
            float h = (t - plateauEnd) / (1.0f - plateauEnd);
            val = 1.0f - 0.6f * h;
        }

        lut[i] = std::max(val, 0.0f);
    }
}

void generateFlatTonalLUT(float* lut, int size)
{
    for (int i = 0; i < size; ++i)
        lut[i] = 1.0f;
}

// ── Profile builder from legacy GrainParams ──────────────────────────────────
//
// Maps the 8 fields of GrainParams to a full FilmStockProfile with sensible
// defaults for the new stochastic parameters.  The old zone-weight triplet
// (shadow/mid/highlight) is converted into a tonal LUT that approximates
// the same Gaussian blend the legacy shader used.

FilmStockProfile buildDefaultProfile(const GrainParams& gp)
{
    FilmStockProfile p;

    // Direct mappings
    p.rms_granularity = gp.rmsGranularity;
    p.grain_size      = gp.size;
    p.iso             = gp.iso;

    // Morphology: ISO >= 200 and finer grain → likely T-Grain stock
    // This is a heuristic; the caller should override for specific stocks.
    p.morphology_type = static_cast<int32_t>(GrainMorphology::Cubic);

    // Chroma micro-contrast from roughness (legacy 'roughness' controlled
    // per-channel decorrelation — conceptually similar)
    p.chroma_micro_contrast = gp.roughness;

    // Spectral matrix: identity (no crosstalk) — legacy model had none
    for (int i = 0; i < 9; ++i)
        p.spectral_matrix[i] = (i % 4 == 0) ? 1.0f : 0.0f;

    // AR coefficients: moderate defaults (generic film)
    p.ar_coefficients[0] = 0.35f;
    p.ar_coefficients[1] = 0.12f;
    p.ar_coefficients[2] = 0.0f;
    p.ar_coefficients[3] = 0.0f;
    p.ar_sigma = 1.0f;

    // Tonal LUT: convert legacy zone weights to a continuous curve.
    // The legacy model used three Gaussian bells at luma = 0.15, 0.50, 0.85
    // with sigma = 0.18, 0.22, 0.18 respectively.
    for (int i = 0; i < kTonalLUTSize; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kTonalLUTSize - 1);

        auto gauss = [](float x, float mu, float sig) -> float {
            float d = (x - mu) / sig;
            return std::exp(-0.5f * d * d);
        };

        float wS = gauss(t, 0.15f, 0.18f) * gp.shadowWeight;
        float wM = gauss(t, 0.50f, 0.22f) * gp.midWeight;
        float wH = gauss(t, 0.85f, 0.18f) * gp.highlightWeight;

        // Scale by amount to bake the old 'amount' slider into the LUT
        p.tonal_lut[i] = (wS + wM + wH) * gp.amount;
    }

    return p;
}

} // namespace MasterFilm
