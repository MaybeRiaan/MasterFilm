// src/presets/StockLibrary.cpp
// Phase 1 film stock definitions.
// Currently only Vision3 500T is active — other stocks will be added
// after tone validation is complete.
//
// TONE PARAMETER AUTHORING
// ─────────────────────────────────────────────────────────────────────────────
// Input params are in SCENE LINEAR units (0.18 = middle grey).
// Output params are normalised [0,1] perceptual targets.
//
// Key reference points in scene linear:
//   0.0022  — practical black floor (~-6.5 stops below grey)
//   0.0225  — toe end (~-3 stops below grey, 0.18 × 2^-3)
//   0.1800  — middle grey (0 stops)
//   1.0000  — diffuse white (~+2.5 stops above grey)
//   4.0000  — shoulder onset (~+4.5 stops, 0.18 × 2^4.5)
//   5.7600  — whitePoint (~+5 stops, 0.18 × 2^5)
//  16.0000  — kLinearMax ceiling (~+6.5 stops above grey)
//
// VISION3 500T DERIVATION
// ─────────────────────────────────────────────────────────────────────────────
// Source: Kodak Vision3 500T datasheet H-1-5219, sensitometric curves (ECN-2)
// Green channel used as primary luminance reference.
// Straight line runs from ~-3 to ~+5 stops — 8 stops of linear response.
// ─────────────────────────────────────────────────────────────────────────────

#include "StockLibrary.h"
#include <algorithm>

namespace MasterFilm {

    StockLibrary& StockLibrary::instance()
    {
        static StockLibrary lib;
        return lib;
    }

    StockLibrary::StockLibrary()
    {
        registerBW();
        registerCinema();
        registerSlide();
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

    void StockLibrary::registerBW()
    {
        // Stocks will be added after Vision3 500T tone validation is complete.
    }

    void StockLibrary::registerCinema()
    {
        // ── Kodak Vision3 500T ────────────────────────────────────────────────────
        // Reference: Kodak Vision3 500T datasheet H-1-5219
        // Sensitometric curves: ECN-2 process, 3200K Tungsten, 1/50 sec
        // Green channel used as primary luminance reference.
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
            p.halation.biasR = 1.0f;    // Tungsten — strong red halation
            p.halation.biasG = 0.35f;
            p.halation.biasB = 0.12f;
            p.halation.outerWeight = 0.30f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.44f;
            p.acutance.rolloff = 0.52f;

            // Input — scene linear, derived from H-1-5219 sensitometric curve
            // Output — normalised perceptual targets
            p.tone.blackPoint = 0.000f;   // true black
            p.tone.toeIn = 0.022f;   // -3 stops below grey (0.18 × 2^-3)
            p.tone.shoulderIn = 4.000f;   // +4.5 stops above grey
            p.tone.whitePoint = 5.760f;   // +5 stops above grey (0.18 × 2^5)
            p.tone.toeOut = 0.080f;   // output at toe/straight boundary
            p.tone.shoulderOut = 0.850f;   // output at straight/shoulder boundary
            p.tone.midGamma = 0.950f;

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

    void StockLibrary::registerSlide()
    {
        // Stocks will be added after Vision3 500T tone validation is complete.
    }

} // namespace MasterFilm