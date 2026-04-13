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

            p.grain.iso = 500.0f;
            p.grain.rmsGranularity = 12.0f;
            p.grain.amount = 0.52f;
            p.grain.size = 0.48f;
            p.grain.roughness = 0.44f;
            p.grain.shadowWeight = 0.40f;
            p.grain.midWeight = 0.44f;
            p.grain.highlightWeight = 0.16f;

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

            // Timing — neutral printer lights (no exposure offset)
            p.timing.printerLightR = 25.0f;
            p.timing.printerLightG = 25.0f;
            p.timing.printerLightB = 25.0f;

            // Print stock — models 2383 print stock contrast amplification.
            // 1.8 gives shadows at ~0.24 and highlights at ~0.88 ACEScct (green),
            // filling the usable scope range. Phase 2 will replace this with
            // the actual 2383 characteristic curve.
            p.print.printGamma = 1.8f;  // used in LINEAR mode only
            p.print.usePrintCurve = true;
            // Per-channel 2383 print curves (from datasheet F002_1254AC)
            // Parameters in rawStops domain: {dMin, dMax, gamma_stops, x0_stops}
            p.print.printRed   = { 0.06f, 3.60f, 0.753f, -1.05f };
            p.print.printGreen = { 0.06f, 3.90f, 0.753f, -1.33f };
            p.print.printBlue  = { 0.06f, 4.20f, 0.813f, -1.51f };
            // Spectral matrix: identity (no print-stock dye cross-talk yet)
            p.print.spectralMatrix = {
                1.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 1.0f
            };

            // Inter-layer coupling from published SMPTE density matrix data.
            // Applied in density space inside the unified processor.
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

            mPresets.push_back(p);
        }
    }

} // namespace MasterFilm