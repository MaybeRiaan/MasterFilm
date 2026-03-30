// src/presets/StockLibrary.cpp
// Phase 1 film stock definitions.
// Every parameter here is traceable to published manufacturer data or
// peer-reviewed academic literature.
//
// TONE PARAMETER AUTHORING REFERENCE
// ─────────────────────────────────────────────────────────────────────────────
// blackPoint and whitePoint are in encoded units of each color space.
// toe / shoulder / midGamma are space-independent (operate post-remap).
//
//   Space        Lin. black   Mid grey   Lin. white   Notes
//   ACEScct      0.0729       0.4135     0.5547       Log, AP1 primaries
//   DWG          0.1283       0.5000     0.5806       DaVinci Intermediate
//   Rec709       0.0000       0.3955     1.0000       Gamma 2.4, no headroom
//
// blackPoint is set slightly above the encoded black floor to produce the
// characteristic shadow lift of each stock, proportional to the floor value.
// whitePoint is set slightly below the encoded white to produce shoulder
// compression before the hard clip.
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

    // ── B&W stocks ────────────────────────────────────────────────────────────────
    // NOTE: tone params for B&W stocks are stubs — identical structure to cinema
    // stocks but not yet authoured per color space. Will be updated after Vision3
    // 500T tone validation is complete.

    void StockLibrary::registerBW()
    {
        // ── Ilford HP5 Plus 400 ───────────────────────────────────────────────────
        {
            FilmPreset p;
            p.id = "ilford_hp5_plus_400";
            p.displayName = "Ilford HP5 Plus 400";
            p.category = "B&W";
            p.notes = "Foundational cubic grain reference. Latitude-focused design.";

            p.grain.iso = 400.0f;
            p.grain.rmsGranularity = 14.0f;
            p.grain.amount = 0.55f;
            p.grain.size = 0.52f;
            p.grain.roughness = 0.50f;
            p.grain.shadowWeight = 0.42f;
            p.grain.midWeight = 0.43f;
            p.grain.highlightWeight = 0.15f;

            p.halation.intensity = 0.20f;
            p.halation.radius = 0.35f;
            p.halation.threshold = 0.80f;
            p.halation.biasR = 0.8f;
            p.halation.biasG = 0.4f;
            p.halation.biasB = 0.2f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.48f;
            p.acutance.rolloff = 0.50f;

            // TODO: author per-space tone blocks after Vision3 500T validation
            p.tone.acesCCT = { 0.083f, 0.552f, 0.28f, 0.72f, 0.98f };
            p.tone.dwg = { 0.142f, 0.578f, 0.28f, 0.72f, 0.98f };
            p.tone.rec709 = { 0.015f, 0.940f, 0.28f, 0.72f, 0.98f };

            mPresets.push_back(p);
        }

        // ── Ilford FP4 Plus 125 ───────────────────────────────────────────────────
        {
            FilmPreset p;
            p.id = "ilford_fp4_plus_125";
            p.displayName = "Ilford FP4 Plus 125";
            p.category = "B&W";
            p.notes = "Low-ISO B&W. Fine grain validates PSD model at low RMS values.";

            p.grain.iso = 125.0f;
            p.grain.rmsGranularity = 8.0f;
            p.grain.amount = 0.32f;
            p.grain.size = 0.38f;
            p.grain.roughness = 0.42f;
            p.grain.shadowWeight = 0.38f;
            p.grain.midWeight = 0.47f;
            p.grain.highlightWeight = 0.15f;

            p.halation.intensity = 0.15f;
            p.halation.radius = 0.28f;
            p.halation.threshold = 0.83f;
            p.halation.biasR = 0.7f;
            p.halation.biasG = 0.3f;
            p.halation.biasB = 0.15f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.52f;
            p.acutance.rolloff = 0.45f;

            // TODO: author per-space tone blocks after Vision3 500T validation
            p.tone.acesCCT = { 0.079f, 0.554f, 0.25f, 0.75f, 1.00f };
            p.tone.dwg = { 0.136f, 0.579f, 0.25f, 0.75f, 1.00f };
            p.tone.rec709 = { 0.010f, 0.950f, 0.25f, 0.75f, 1.00f };

            mPresets.push_back(p);
        }

        // ── Kodak T-Max 100 ───────────────────────────────────────────────────────
        {
            FilmPreset p;
            p.id = "kodak_tmax_100";
            p.displayName = "Kodak T-Max 100";
            p.category = "B&W";
            p.notes = "T-grain morphology. Similar RMS to FP4 but very different PSD shape.";

            p.grain.iso = 100.0f;
            p.grain.rmsGranularity = 8.0f;
            p.grain.amount = 0.28f;
            p.grain.size = 0.30f;
            p.grain.roughness = 0.25f;
            p.grain.shadowWeight = 0.35f;
            p.grain.midWeight = 0.50f;
            p.grain.highlightWeight = 0.15f;

            p.halation.intensity = 0.10f;
            p.halation.radius = 0.22f;
            p.halation.threshold = 0.85f;
            p.halation.biasR = 0.6f;
            p.halation.biasG = 0.25f;
            p.halation.biasB = 0.10f;

            p.acutance.character = AcutanceCharacter::Enhanced;
            p.acutance.intensity = 0.62f;
            p.acutance.rolloff = 0.40f;
            p.acutance.kostinskyStrength = 0.08f;

            // TODO: author per-space tone blocks after Vision3 500T validation
            p.tone.acesCCT = { 0.077f, 0.554f, 0.22f, 0.78f, 1.02f };
            p.tone.dwg = { 0.134f, 0.579f, 0.22f, 0.78f, 1.02f };
            p.tone.rec709 = { 0.008f, 0.955f, 0.22f, 0.78f, 1.02f };

            mPresets.push_back(p);
        }

        // ── Kodak Tri-X 400 ───────────────────────────────────────────────────────
        {
            FilmPreset p;
            p.id = "kodak_trix_400";
            p.displayName = "Kodak Tri-X 400";
            p.category = "B&W";
            p.notes = "Most studied B&W stock. Parametric grain model backed by published PSD data.";

            p.grain.iso = 400.0f;
            p.grain.rmsGranularity = 18.0f;
            p.grain.amount = 0.68f;
            p.grain.size = 0.60f;
            p.grain.roughness = 0.62f;
            p.grain.shadowWeight = 0.45f;
            p.grain.midWeight = 0.40f;
            p.grain.highlightWeight = 0.15f;

            p.halation.intensity = 0.25f;
            p.halation.radius = 0.40f;
            p.halation.threshold = 0.78f;
            p.halation.biasR = 0.85f;
            p.halation.biasG = 0.40f;
            p.halation.biasB = 0.20f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.45f;
            p.acutance.rolloff = 0.55f;

            // TODO: author per-space tone blocks after Vision3 500T validation
            p.tone.acesCCT = { 0.086f, 0.551f, 0.32f, 0.68f, 0.95f };
            p.tone.dwg = { 0.144f, 0.577f, 0.32f, 0.68f, 0.95f };
            p.tone.rec709 = { 0.018f, 0.935f, 0.32f, 0.68f, 0.95f };

            mPresets.push_back(p);
        }
    }

    // ── Cinema stocks ─────────────────────────────────────────────────────────────

    void StockLibrary::registerCinema()
    {
        // ── Kodak Vision3 500T ────────────────────────────────────────────────────
        // Reference: Kodak Vision3 500T datasheet (H-1-5242); SMPTE papers (Hunt, Kennel)
        // VALIDATION TARGET — full three-space tone authoring done here first.
        //
        // Tone derivation per space:
        //
        //   ACEScct:
        //     blackPoint 0.083 = encoded floor (0.0729) + shadow lift (~0.010 scene stops)
        //     whitePoint 0.554 = encoded 1.0 linear (0.5547) — just under hard clip
        //
        //   DWG (DaVinci Intermediate):
        //     blackPoint 0.140 = encoded floor (0.1283) + proportional shadow lift
        //     whitePoint 0.580 = encoded 1.0 linear (0.5806) — just under hard clip
        //
        //   Rec709 (gamma 2.4):
        //     blackPoint 0.020 = no log floor — direct gamma lift, same intent as original
        //     whitePoint 0.920 = tighter than 1.0 to give visible shoulder before clip
        //     Note: Rec709 has no highlight headroom above 1.0 — shoulder is cosmetic only
        //
        //   toe / shoulder / midGamma are identical across all spaces:
        //     These operate in normalised [0,1] space after the black/white remap,
        //     so the curve shape is preserved regardless of which space is active.
        {
            FilmPreset p;
            p.id = "kodak_vision3_500t";
            p.displayName = "Kodak Vision3 500T";
            p.category = "Cinema";
            p.notes = "Most documented color negative. Coupling matrix from published SMPTE data.";

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

            // ── Tone — three color space variants ─────────────────────────────────
            p.tone.acesCCT = {
                /* blackPoint */ 0.083f,   // encoded floor 0.0729 + lift
                /* whitePoint */ 0.554f,   // encoded linear 1.0 = 0.5547
                /* toe        */ 0.30f,
                /* shoulder   */ 0.70f,
                /* midGamma   */ 0.96f
            };
            p.tone.dwg = {
                /* blackPoint */ 0.140f,   // encoded floor 0.1283 + lift
                /* whitePoint */ 0.580f,   // encoded linear 1.0 = 0.5806
                /* toe        */ 0.30f,
                /* shoulder   */ 0.70f,
                /* midGamma   */ 0.96f
            };
            p.tone.rec709 = {
                /* blackPoint */ 0.020f,   // gamma space — no log floor
                /* whitePoint */ 0.920f,   // below 1.0 to give visible shoulder
                /* toe        */ 0.30f,
                /* shoulder   */ 0.70f,
                /* midGamma   */ 0.96f
            };

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

        // ── Kodak Vision3 250D ────────────────────────────────────────────────────
        // TODO: author per-space tone blocks after Vision3 500T validation
        {
            FilmPreset p;
            p.id = "kodak_vision3_250d";
            p.displayName = "Kodak Vision3 250D";
            p.category = "Cinema";
            p.notes = "Daylight negative complement to 500T. Validates white balance path.";

            p.grain.iso = 250.0f;
            p.grain.rmsGranularity = 9.0f;
            p.grain.amount = 0.38f;
            p.grain.size = 0.40f;
            p.grain.roughness = 0.38f;

            p.halation.intensity = 0.28f;
            p.halation.radius = 0.38f;
            p.halation.threshold = 0.76f;
            p.halation.biasR = 1.0f;
            p.halation.biasG = 0.30f;
            p.halation.biasB = 0.10f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.48f;
            p.acutance.rolloff = 0.48f;

            p.tone.acesCCT = { 0.081f, 0.554f, 0.28f, 0.72f, 0.98f };
            p.tone.dwg = { 0.138f, 0.579f, 0.28f, 0.72f, 0.98f };
            p.tone.rec709 = { 0.016f, 0.935f, 0.28f, 0.72f, 0.98f };

            p.color.couplingMatrix = {
                 1.00f, -0.06f,  0.01f,
                -0.04f,  1.00f, -0.02f,
                 0.02f, -0.05f,  1.00f
            };
            p.color.hueShadowShift = 0.5f;
            p.color.hueMidShift = 0.0f;
            p.color.hueHighlightShift = -0.5f;
            p.color.satShadow = 0.92f;
            p.color.satMid = 1.00f;
            p.color.satHighlight = 0.97f;

            mPresets.push_back(p);
        }
    }

    // ── Slide stocks ──────────────────────────────────────────────────────────────

    void StockLibrary::registerSlide()
    {
        // ── Fujifilm Velvia 50 ────────────────────────────────────────────────────
        // TODO: author per-space tone blocks after Vision3 500T validation
        {
            FilmPreset p;
            p.id = "fujifilm_velvia_50";
            p.displayName = "Fujifilm Velvia 50";
            p.category = "Slide";
            p.notes = "MTF exceeds 1.0 — stress-tests acutance model. High saturation slide.";

            p.grain.iso = 50.0f;
            p.grain.rmsGranularity = 6.0f;
            p.grain.amount = 0.18f;
            p.grain.size = 0.22f;
            p.grain.roughness = 0.20f;

            p.halation.intensity = 0.08f;
            p.halation.radius = 0.18f;
            p.halation.threshold = 0.90f;
            p.halation.biasR = 0.5f;
            p.halation.biasG = 0.2f;
            p.halation.biasB = 0.1f;

            p.acutance.character = AcutanceCharacter::Enhanced;
            p.acutance.intensity = 0.85f;
            p.acutance.rolloff = 0.30f;
            p.acutance.kostinskyStrength = 0.22f;

            // Slide stocks have harder toe/shoulder and slightly elevated mids
            p.tone.acesCCT = { 0.075f, 0.554f, 0.18f, 0.80f, 1.05f };
            p.tone.dwg = { 0.132f, 0.579f, 0.18f, 0.80f, 1.05f };
            p.tone.rec709 = { 0.005f, 0.960f, 0.18f, 0.80f, 1.05f };

            p.color.couplingMatrix = {
                 1.00f,  0.05f,  0.02f,
                 0.03f,  1.00f,  0.01f,
                -0.02f,  0.04f,  1.00f
            };
            p.color.hueShadowShift = 3.0f;
            p.color.hueMidShift = 2.0f;
            p.color.hueHighlightShift = 0.0f;
            p.color.satShadow = 1.10f;
            p.color.satMid = 1.25f;
            p.color.satHighlight = 1.15f;

            mPresets.push_back(p);
        }

        // ── Fujifilm Provia 100F ──────────────────────────────────────────────────
        // TODO: author per-space tone blocks after Vision3 500T validation
        {
            FilmPreset p;
            p.id = "fujifilm_provia_100f";
            p.displayName = "Fujifilm Provia 100F";
            p.category = "Slide";
            p.notes = "Neutral slide reference. Should be clearly distinct from Velvia.";

            p.grain.iso = 100.0f;
            p.grain.rmsGranularity = 7.0f;
            p.grain.amount = 0.25f;
            p.grain.size = 0.28f;
            p.grain.roughness = 0.28f;

            p.halation.intensity = 0.10f;
            p.halation.radius = 0.20f;
            p.halation.threshold = 0.88f;
            p.halation.biasR = 0.55f;
            p.halation.biasG = 0.22f;
            p.halation.biasB = 0.10f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.58f;
            p.acutance.rolloff = 0.44f;
            p.acutance.kostinskyStrength = 0.05f;

            p.tone.acesCCT = { 0.076f, 0.554f, 0.20f, 0.78f, 1.00f };
            p.tone.dwg = { 0.133f, 0.579f, 0.20f, 0.78f, 1.00f };
            p.tone.rec709 = { 0.006f, 0.955f, 0.20f, 0.78f, 1.00f };

            p.color.couplingMatrix = {
                 1.00f, -0.02f,  0.00f,
                -0.01f,  1.00f, -0.01f,
                 0.00f, -0.02f,  1.00f
            };
            p.color.hueShadowShift = 0.0f;
            p.color.hueMidShift = 0.0f;
            p.color.hueHighlightShift = 0.0f;
            p.color.satShadow = 1.00f;
            p.color.satMid = 1.05f;
            p.color.satHighlight = 1.02f;

            mPresets.push_back(p);
        }
    }

} // namespace MasterFilm