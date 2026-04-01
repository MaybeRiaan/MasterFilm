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
    enum class ColorSpaceMode {
        ACEScct = 0,       // ACEScct / AP1 — log, middle grey at 0.4135
        DaVinciWideGamut,  // DaVinci Wide Gamut / DaVinci Intermediate — middle grey at 0.5
        Rec709             // Rec.709 gamma 2.4 / DaVinci YRGB
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
    // All values are in SCENE LINEAR units, normalised so 0.18 = middle grey.
    // Values above 1.0 are valid and expected — the LUT covers 0 to kLinearMax.
    //
    // Derived from published H&D sensitometric curves (see StockLibrary.cpp).
    // The curve always operates on scene linear light — color space conversion
    // is handled internally by ToneProcessor via ColorSpaceTransform.h.
    //
    //   blackPoint — linear value mapped to output 0.0 (D-min region)
    //   whitePoint — linear value mapped to output 1.0 (shoulder region)
    //   toe        — normalised inflection [0,1] in remapped space
    //   shoulder   — normalised inflection [0,1] in remapped space
    //   midGamma   — power in the straight-line region (< 1.0 compresses mids)
    struct ToneParams {
        float blackPoint = 0.002f;  // ~-6.5 stops below grey — D-min
        float whitePoint = 6.0f;    // ~+5 stops above grey — shoulder region
        float toe = 0.35f;
        float shoulder = 0.80f;
        float midGamma = 1.0f;
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
        ToneParams     tone;     // single block — space-independent, scene linear units
        ColorParams    color;

        std::string closestStockId;
        float       closestStockConfidence = 0.0f;
    };

} // namespace MasterFilm