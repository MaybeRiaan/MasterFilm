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
    // Determines which ToneParams block is selected at render time.
    // All tone anchor points (blackPoint, whitePoint) are authored in the
    // encoded units of the corresponding color space — see StockLibrary.cpp
    // for the reference values used when authoring each block.
    enum class ColorSpaceMode {
        ACEScct = 0,       // ACEScct / AP1 — log, middle grey at 0.4135
        DaVinciWideGamut,  // DaVinci Wide Gamut / DaVinci Intermediate — middle grey at 0.5
        Rec709             // Rec.709 gamma 2.4 / DaVinci YRGB — middle grey at ~0.4
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
    // blackPoint and whitePoint are in the encoded units of the target color space.
    // toe, shoulder, midGamma operate in normalised [0,1] space after remapping,
    // so they are the same across all color space variants.
    //
    // Reference anchor values per space:
    //   Space        Linear black   Middle grey   Linear white
    //   ACEScct      0.0729         0.4135        0.5547
    //   DWG          0.1283         0.5000        0.5806
    //   Rec709       0.0000         0.3955        1.0000
    struct ToneParams {
        float blackPoint = 0.0f;
        float whitePoint = 1.0f;
        float toe = 0.3f;
        float shoulder = 0.7f;
        float midGamma = 1.0f;
    };

    // ── Tone param set — one block per supported color space ──────────────────────
    struct ToneParamSet {
        ToneParams acesCCT;
        ToneParams dwg;
        ToneParams rec709;

        // Convenience selector — call from onRender with the current mode
        const ToneParams& forMode(ColorSpaceMode mode) const {
            switch (mode) {
            case ColorSpaceMode::ACEScct:          return acesCCT;
            case ColorSpaceMode::DaVinciWideGamut: return dwg;
            case ColorSpaceMode::Rec709:           return rec709;
            }
            return acesCCT; // fallback — should never be reached
        }
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
        ToneParamSet tone;   // was: ToneParams tone
        ColorParams    color;

        std::string closestStockId;
        float       closestStockConfidence = 0.0f;
    };

} // namespace MasterFilm