// src/presets/FilmPreset.h
// Central data structure representing a complete film stock preset.
// All five characteristic blocks live here so processors can read them
// without knowing anything about the UI or parameter system.
#pragma once

#include <string>
#include <array>

namespace MasterFilm {

    // ── Color space mode ──────────────────────────────────────────────────────────
    // Set by the user via a dropdown parameter in the plugin UI.
    // Tells the plugin which transfer function to use for the internal
    // scene-linear round trip. The tone curve itself is space-independent —
    // it always operates in scene linear light.
    //
    // MasterFilm requires a scene-referred wide gamut input space.
    // Rec.709 is NOT supported — it is display-referred with no highlight
    // headroom above 1.0, which is incompatible with the H&D curve model.
    // Users on a Rec.709 timeline should sandwich the plugin between two
    // CST nodes: Rec709 → ACEScct (input), ACEScct → Rec709 (output).
    enum class ColorSpaceMode {
        ACEScct = 0,      // ACEScct / AP1 — log, middle grey at 0.4135
        DaVinciWideGamut  // DaVinci Wide Gamut / DaVinci Intermediate — middle grey at 0.5
    };

    // ── Grain ─────────────────────────────────────────────────────────────────────
    struct GrainParams {
        float amount = 0.5f;
        float size = 0.5f;
        float roughness = 0.5f;
        float shadowWeight = 0.40f;
        float midWeight = 0.45f;
        float highlightWeight = 0.15f;
        float rmsGranularity = 10.0f;
        float iso = 400.0f;
    };

    // ── Halation ──────────────────────────────────────────────────────────────────
    struct HalationParams {
        float intensity = 0.3f;
        float radius = 0.5f;
        float threshold = 0.7f;
        float biasR = 1.0f;
        float biasG = 0.3f;
        float biasB = 0.1f;
        float outerRadiusScale = 4.0f;
        float outerWeight = 0.25f;
    };

    // ── Acutance ──────────────────────────────────────────────────────────────────
    enum class AcutanceCharacter {
        Soft,
        Natural,
        Enhanced
    };

    struct AcutanceParams {
        AcutanceCharacter character = AcutanceCharacter::Natural;
        float             intensity = 0.5f;
        float             rolloff = 0.5f;
        float             kostinskyStrength = 0.0f;
    };

    // ── Tone ──────────────────────────────────────────────────────────────────────
    // Input parameters are in SCENE LINEAR units (0.18 = middle grey).
    // Output parameters are NORMALISED [0,1] perceptual targets.
    //
    // This separation allows physical accuracy on the input side (values
    // traceable to published sensitometric data) while maintaining perceptual
    // correctness on the output side (the image looks right).
    //
    // INPUT — scene linear, traceable to H&D curve:
    //   blackPoint — linear value that maps to output 0.0 (below D-min)
    //   toeIn      — linear value where toe ends / straight line begins
    //   shoulderIn — linear value where straight line ends / shoulder begins
    //   whitePoint — linear value that maps to output 1.0 (shoulder ceiling)
    //
    // OUTPUT — normalised [0,1] perceptual targets:
    //   toeOut      — output level at the toe/straight boundary
    //   shoulderOut — output level at the straight/shoulder boundary
    //
    // SHAPE:
    //   midGamma — power in the straight-line region (< 1.0 compresses mids)
    //
    // Reference stop offsets for input values (0.18 = 0 stops):
    //   Stop    Linear value   Notes
    //   -6.5    0.0022         practical black floor
    //   -3.0    0.0225         typical toe onset (Vision3 500T)
    //    0.0    0.1800         middle grey
    //   +2.5    1.0000         diffuse white
    //   +4.5    4.0000         Vision3 500T shoulder onset
    //   +5.0    5.7600         Vision3 500T whitePoint
    //
    // NOTE — Option B (future refinement):
    // Replace perceptual output targets with density-to-scan pipeline using
    // print film data (e.g. Kodak Vision Premier 2383). Input params are already
    // in correct physical units for that transition.
    struct ToneParams {
        // Input — scene linear
        float blackPoint = 0.000f;    // true black
        float toeIn = 0.022f;    // ~-3 stops below grey
        float shoulderIn = 4.000f;    // ~+4.5 stops above grey
        float whitePoint = 5.760f;    // ~+5 stops above grey

        // Output — normalised perceptual targets [0,1]
        float toeOut = 0.080f;    // output level at toe/straight boundary
        float shoulderOut = 0.850f;    // output level at straight/shoulder boundary

        // Shape
        float midGamma = 1.000f;    // power in straight-line region
    };

    // ── Color / inter-layer coupling ──────────────────────────────────────────────
    struct ColorParams {
        std::array<float, 9> couplingMatrix = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };
        float hueShadowShift = 0.0f;
        float hueMidShift = 0.0f;
        float hueHighlightShift = 0.0f;
        float satShadow = 1.0f;
        float satMid = 1.0f;
        float satHighlight = 1.0f;
    };

    // ── Complete preset ────────────────────────────────────────────────────────────
    struct FilmPreset {
        std::string id;
        std::string displayName;
        std::string category;
        std::string notes;

        GrainParams    grain;
        HalationParams halation;
        AcutanceParams acutance;
        ToneParams     tone;
        ColorParams    color;

        std::string closestStockId;
        float       closestStockConfidence = 0.0f;
    };

} // namespace MasterFilm