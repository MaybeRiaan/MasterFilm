// src/presets/StockLibrary.cpp
// Film stock definitions — Vision3 500T only during tone validation.
//
// PER-CHANNEL CURVE DERIVATION
// ─────────────────────────────────────────────────────────────────────────────
// Source: Kodak Vision3 500T datasheet H-1-5219, sensitometric curves
//         Exposure: 3200K Tungsten 1/50 sec, Process: ECN-2
//         Densitometry: ECN-2 (Status-M)
//
// Values read from published sensitometric chart, camera stops x-axis:
//
//              Red         Green       Blue
//   dMin:      0.15        0.25        0.45       base fog
//   dMax:      1.80        2.30        2.85       max density
//   gamma:     0.20        0.25        0.30       density/stop (straight line)
//   toeEnd:   -3.0        -3.0        -3.0       enters straight line (for x0)
//   shoulder: +6.0        +6.5        +5.0       exits straight line (for x0)
//   x0:        1.50        1.75        1.00       sigmoid inflection = (toeEnd+shoulder)/2
//
// Piecewise toeStart/clip fields removed — sigmoid model does not need them.
//
// Green channel is the luminance reference. Red and blue diverge to
// produce the stock's colour signature — controlled by filmColor param.
// ─────────────────────────────────────────────────────────────────────────────

#include "StockLibrary.h"

namespace MasterFilm {

    StockLibrary& StockLibrary::instance()
    {
        static StockLibrary lib;
        return lib;
    }

    StockLibrary::StockLibrary()
    {
        registerCinema();
    }

    const FilmPreset* StockLibrary::findById(const std::string& id) const
    {
        for (const auto& p : mPresets)
            if (p.id == id) return &p;
        return nullptr;
    }

    std::vector<const FilmPreset*> StockLibrary::byCategory(const std::string& cat) const
    {
        std::vector<const FilmPreset*> out;
        for (const auto& p : mPresets)
            if (p.category == cat) out.push_back(&p);
        return out;
    }

    void StockLibrary::registerCinema()
    {
        // ── Kodak Vision3 500T ────────────────────────────────────────────────────
        // Reference: Kodak Vision3 500T datasheet H-1-5219
        // Sensitometric curves: ECN-2 process, 3200K Tungsten, 1/50 sec
        {
            FilmPreset p;
            p.id = "kodak_vision3_500t";
            p.displayName = "Kodak Vision3 500T";
            p.category = "Cinema";
            p.notes = "Derived from H-1-5219 sensitometric data. ECN-2 process.";

            p.halation.intensity = 0.35f;
            p.halation.radius = 0.45f;
            p.halation.threshold = 0.72f;
            p.halation.biasR = 1.0f;
            p.halation.biasG = 0.35f;
            p.halation.biasB = 0.12f;
            p.halation.outerWeight = 0.30f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.44f;
            p.acutance.rolloff = 0.52f;

            // ── Per-channel H&D curves (sigmoid model) ───────────────────────
            // Source: Kodak Vision3 500T datasheet H-1-5219, ECN-2 process
            // Sigmoid: D = dMin + (dMax-dMin) / (1 + exp(-k*(stops-x0)))
            // k = 4*gamma/(dMax-dMin)  derived at runtime
            // x0 = (toeEnd + shoulder) / 2  from datasheet geometry

            // Red — lowest Dmax, compresses highlights earliest
            // toeEnd=-3.0, shoulder=+6.0 → x0=1.50
            p.tone.red.dMin = 0.15f;
            p.tone.red.dMax = 1.80f;
            p.tone.red.gamma = 0.20f;
            p.tone.red.x0 = 1.50f;

            // Green — luminance reference, middle Dmax
            // toeEnd=-3.0, shoulder=+6.5 → x0=1.75
            p.tone.green.dMin = 0.25f;
            p.tone.green.dMax = 2.30f;
            p.tone.green.gamma = 0.25f;
            p.tone.green.x0 = 1.75f;

            // Blue — highest Dmax, steepest gamma, rolls off earliest
            // toeEnd=-3.0, shoulder=+5.0 → x0=1.00
            p.tone.blue.dMin = 0.45f;
            p.tone.blue.dMax = 2.85f;
            p.tone.blue.gamma = 0.30f;
            p.tone.blue.x0 = 1.00f;

            // Film colour at full — user can dial down to 0 for tone-only
            p.tone.filmColor = 0.55f;

            // Print gamma — models 2383 print stock contrast amplification.
            // 1.8 gives shadows at ~0.24 and highlights at ~0.88 ACEScct (green),
            // filling the usable scope range. Phase 2 will replace this with
            // the actual 2383 characteristic curve.
            p.tone.printGamma = 1.8f;

            // Inter-layer coupling from published SMPTE density matrix data
            p.color.couplingMatrix = {
                 1.00f, -0.08f,  0.02f,
                -0.05f,  1.00f, -0.03f,
                 0.03f, -0.06f,  1.00f
            };
            p.color.hueShadowShift = 2.0f;
            p.color.hueMidShift = 0.0f;
            p.color.hueHighlightShift = -1.5f;
            p.color.satShadow = 0.90f;
            p.color.satMid = 1.00f;
            p.color.satHighlight = 0.95f;
            p.color.orangeMask = false;

            // ── Stochastic grain profile (FilmStockProfile) ─────────────
            // Populates the full GPU-transferable grain model for 500T.
            //
            // Source data:
            //   RMS granularity: Kodak H-1-5219, diffuse RMS = 12
            //   Morphology: T-Grain (Vision3 uses tabular grain technology)
            //   AR coefficients: moderate clumping — DLT controls agglomeration
            //   Spectral matrix: tungsten bias — 3200K illuminant under-exposes
            //     the blue-sensitive layer, producing heavier blue-channel grain
            //     and stronger inter-layer coupling from blue → green/red
            //   Tonal LUT: DLT curve — shadow noise suppression via Dye Layering
            //     Technology, peak at mid-tones, gentle highlight rolloff
            //   Chroma micro-contrast: moderate — DLT reduces but does not
            //     eliminate dye cloud misalignment between layers
            {
                FilmStockProfile& gp = p.grainProfile;

                gp.rms_granularity = 12.0f;
                gp.iso             = 500.0f;
                gp.grain_size      = 0.48f;

                // T-Grain: Vision3 tabular grain crystals — log-normal radius
                // distribution, flatter coverage, tighter perceived variance
                // than the cubic crystals used in Tri-X / Double-X.
                gp.morphology_type = static_cast<int32_t>(GrainMorphology::TGrain);

                // AR spatial correlation — moderate, controlled clumping.
                // T-Grain + DLT produces a more uniform grain field than
                // classic cubic stocks; lower a1 than Tri-X (0.55).
                gp.ar_coefficients[0] = 0.42f;  // ring-1 (4-connected)
                gp.ar_coefficients[1] = 0.18f;  // ring-1 diagonals
                gp.ar_coefficients[2] = 0.06f;  // ring-2 (2-pixel axis)
                gp.ar_coefficients[3] = 0.00f;  // ring-2 diagonals (negligible)
                gp.ar_sigma = 1.0f;

                // Spectral matrix — tungsten (3200K) bias.
                // Under tungsten illumination the blue-sensitive layer receives
                // ~2 stops less light than red/green, resulting in:
                //   - Higher blue-layer grain (under-exposure → more visible AgX)
                //   - Stronger blue → green/red coupling (spectral overlap of
                //     the yellow filter layer is imperfect under low-blue light)
                //
                // Row-major: [R→R, G→R, B→R,  R→G, G→G, B→G,  R→B, G→B, B→B]
                gp.spectral_matrix[0] = 1.00f;  // R→R
                gp.spectral_matrix[1] = 0.12f;  // G→R
                gp.spectral_matrix[2] = 0.04f;  // B→R
                gp.spectral_matrix[3] = 0.08f;  // R→G
                gp.spectral_matrix[4] = 1.00f;  // G→G
                gp.spectral_matrix[5] = 0.10f;  // B→G  (blue coupling into green)
                gp.spectral_matrix[6] = 0.06f;  // R→B
                gp.spectral_matrix[7] = 0.15f;  // G→B  (strongest off-diagonal —
                                                 //  green dye interlayer scatter
                                                 //  into under-exposed blue layer)
                gp.spectral_matrix[8] = 1.00f;  // B→B

                // Chroma micro-contrast — moderate.
                // DLT suppresses dye cloud misalignment but doesn't eliminate
                // it.  0.35 gives subtle colour grain without the heavy chroma
                // noise seen in reversal stocks (Ektachrome ≈ 0.55).
                gp.chroma_micro_contrast = 0.35f;

                // Tonal LUT — DLT shadow-suppression curve.
                // Vision3's Dye Layering Technology smooths shadow grain by
                // optimising dye cloud placement in low-density regions.
                //   shadowFloor    = 0.25 (shadow grain reduced to 25% of peak)
                //   midPeak        = 1.00 (full grain at mid-tones)
                //   highlightRolloff = 1.20 (gentle — 500T has usable highlights)
                generateDLTTonalLUT(gp.tonal_lut, kTonalLUTSize,
                                    0.25f, 1.0f, 1.2f);

                // Deterministic seed — arbitrary, just needs to be consistent
                gp.global_seed    = 0x5EED0042u;
                gp.frame_index    = 0;
                gp.overlap_pixels = 16;
            }

            mPresets.push_back(p);
        }
    }

} // namespace MasterFilm