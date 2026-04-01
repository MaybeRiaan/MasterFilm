// src/presets/StockLibrary.cpp
// Phase 1 film stock definitions.
//
// TONE PARAMETER AUTHORING
// ─────────────────────────────────────────────────────────────────────────────
// All ToneParams values are in SCENE LINEAR units, normalised so 0.18 = 18%
// grey (middle grey). Values above 1.0 are valid and expected.
//
// The tone curve is space-independent — ToneProcessor converts to/from scene
// linear internally using ColorSpaceTransform.h. Stock authors do not need
// to think about ACEScct, DWG, or Rec709 encoding here.
//
// Key reference points in scene linear:
//   0.0018  — practical black floor (~-6.5 stops below grey)
//   0.18    — middle grey (0 stops)
//   1.0     — diffuse white (~+2.5 stops above grey)
//   6.0     — ~+5 stops above grey (Vision3 shoulder region)
//   16.0    — kLinearMax ceiling (~+6.5 stops above grey)
//
// VISION3 500T DERIVATION (reference stock)
// ─────────────────────────────────────────────────────────────────────────────
// Source: Kodak Vision3 500T datasheet H-1-5219, sensitometric curves (ECN-2)
// Green channel used as primary luminance reference.
//
// Reading the H&D curve, green channel:
//   Camera stops   Linear value         Density (G)   Region
//   -8             0.18 × 2^-8 = 0.0007   ~0.60      D-min
//   -5             0.18 × 2^-5 = 0.0056   ~0.65      deep toe
//   -3             0.18 × 2^-3 = 0.0225   ~0.80      toe end
//   -1             0.18 × 2^-1 = 0.09     ~1.20      straight line
//    0             0.18                   ~1.40      middle grey
//   +2             0.18 × 2^2  = 0.72     ~1.80      straight line
//   +5             0.18 × 2^5  = 5.76     ~2.20      shoulder start
//   +8             0.18 × 2^8  = 46.0     ~2.50      shoulder (above kLinearMax)
//
// The straight line runs from ~-3 to ~+5 stops — 8 stops of linear response.
// Toe begins below -3 stops. Shoulder begins above +5 stops.
// This matches Kodak's "2 stops of extended highlight latitude" marketing claim.
//
// Parameter derivation:
//   blackPoint 0.002  = midpoint of D-min region (~-6.5 stops, 0.0007–0.006)
//   whitePoint 6.0    = shoulder onset (~+5 stops, 0.18 × 2^5 = 5.76)
//   toe        0.35   = long gradual toe — rolls off early, very gentle
//   shoulder   0.80   = late shoulder — consistent with 8-stop linear region
//   midGamma   0.95   = slight mid compression from curve slope reading
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
    // Tone params are plausible estimates — will be derived from datasheets
    // after Vision3 500T tone validation is complete.

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

            // TODO: derive from Ilford HP5 datasheet after Vision3 500T validation
            p.tone.blackPoint = 0.002f;
            p.tone.whitePoint = 5.0f;
            p.tone.toe = 0.32f;
            p.tone.shoulder = 0.75f;
            p.tone.midGamma = 0.98f;

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

            // TODO: derive from Ilford FP4 datasheet
            p.tone.blackPoint = 0.001f;
            p.tone.whitePoint = 4.5f;
            p.tone.toe = 0.28f;
            p.tone.shoulder = 0.78f;
            p.tone.midGamma = 1.00f;

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

            // TODO: derive from Kodak T-Max 100 datasheet
            p.tone.blackPoint = 0.001f;
            p.tone.whitePoint = 4.0f;
            p.tone.toe = 0.25f;
            p.tone.shoulder = 0.80f;
            p.tone.midGamma = 1.02f;

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

            // TODO: derive from Kodak Tri-X datasheet
            p.tone.blackPoint = 0.003f;
            p.tone.whitePoint = 5.5f;
            p.tone.toe = 0.38f;
            p.tone.shoulder = 0.72f;
            p.tone.midGamma = 0.95f;

            mPresets.push_back(p);
        }
    }

    // ── Cinema stocks ─────────────────────────────────────────────────────────────

    void StockLibrary::registerCinema()
    {
        // ── Kodak Vision3 500T ────────────────────────────────────────────────────
        // Reference: Kodak Vision3 500T datasheet H-1-5219
        // Sensitometric curves: ECN-2 process, 3200K Tungsten, 1/50 sec
        // Green channel used as primary luminance reference.
        // See derivation notes at top of this file.
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
            p.halation.biasR = 1.0f;   // Tungsten — strong red halation
            p.halation.biasG = 0.35f;
            p.halation.biasB = 0.12f;
            p.halation.outerWeight = 0.30f;

            p.acutance.character = AcutanceCharacter::Natural;
            p.acutance.intensity = 0.44f;
            p.acutance.rolloff = 0.52f;

            // ── Tone — derived from H&D sensitometric curve, green channel ─────────
            // blackPoint: D-min region, linear ~-6.5 stops below grey
            // whitePoint: shoulder onset, ~+5 stops above grey (0.18 × 2^5 = 5.76)
            // toe:        0.35 — very long, gradual rolloff (toe starts at -3 stops)
            // shoulder:   0.80 — late shoulder consistent with 8-stop linear region
            // midGamma:   0.95 — slight compression from straight-line slope reading
            p.tone.blackPoint = 0.002f;
            p.tone.whitePoint = 6.0f;
            p.tone.toe = 0.35f;
            p.tone.shoulder = 0.80f;
            p.tone.midGamma = 0.95f;

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
        // TODO: derive from Kodak Vision3 250D datasheet H-1-5241
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

            p.tone.blackPoint = 0.002f;
            p.tone.whitePoint = 5.5f;
            p.tone.toe = 0.32f;
            p.tone.shoulder = 0.78f;
            p.tone.midGamma = 0.98f;

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
        // TODO: derive from Fujifilm Velvia 50 technical data
        // Note: slide film has harder toe/shoulder and narrower latitude than negative
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

            // Slide: narrower latitude, harder clip — shoulder starts earlier
            // whitePoint ~+3 stops (0.18 × 2^3 = 1.44) reflects slide's limited headroom
            p.tone.blackPoint = 0.003f;
            p.tone.whitePoint = 1.5f;
            p.tone.toe = 0.20f;
            p.tone.shoulder = 0.72f;
            p.tone.midGamma = 1.05f;

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
        // TODO: derive from Fujifilm Provia 100F technical data
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

            // Slide: slightly more latitude than Velvia but still narrower than negative
            p.tone.blackPoint = 0.002f;
            p.tone.whitePoint = 1.8f;
            p.tone.toe = 0.22f;
            p.tone.shoulder = 0.75f;
            p.tone.midGamma = 1.00f;

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