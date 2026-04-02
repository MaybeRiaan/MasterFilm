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
//   toeStart: -5.0        -5.5        -6.0       lifts off base fog
//   toeEnd:   -3.0        -3.0        -3.0       enters straight line
//   shoulder: +6.0        +6.5        +5.0       exits straight line
//   clip:     +7.5        +8.0        +7.0       reaches Dmax
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

            // ── Per-channel H&D curves from sensitometric data ────────────────

            // Red — lowest Dmax, compresses highlights earliest
            p.tone.red.toeStartStops  = -5.0f;
            p.tone.red.toeEndStops    = -3.0f;
            p.tone.red.shoulderStops  =  6.0f;
            p.tone.red.clipStops      =  7.5f;
            p.tone.red.dMin           =  0.15f;
            p.tone.red.dMax           =  1.80f;
            p.tone.red.gamma          =  0.20f;

            // Green — luminance reference, middle Dmax
            p.tone.green.toeStartStops = -5.5f;
            p.tone.green.toeEndStops   = -3.0f;
            p.tone.green.shoulderStops =  6.5f;
            p.tone.green.clipStops     =  8.0f;
            p.tone.green.dMin          =  0.25f;
            p.tone.green.dMax          =  2.30f;
            p.tone.green.gamma         =  0.25f;

            // Blue — highest Dmax, steepest gamma, rolls off earliest
            p.tone.blue.toeStartStops  = -6.0f;
            p.tone.blue.toeEndStops    = -3.0f;
            p.tone.blue.shoulderStops  =  5.0f;
            p.tone.blue.clipStops      =  7.0f;
            p.tone.blue.dMin           =  0.45f;
            p.tone.blue.dMax           =  2.85f;
            p.tone.blue.gamma          =  0.30f;

            // Film colour at full — user can dial down to 0 for tone-only
            p.tone.filmColor = 1.0f;

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

            mPresets.push_back(p);
        }
    }

} // namespace MasterFilm
