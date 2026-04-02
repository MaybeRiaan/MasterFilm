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
    //
    // PHYSICAL MODEL
    // ─────────────────────────────────────────────────────────────────────────────
    // The tone processor models three physical stages:
    //
    //   1. Exposure:     scene linear → log exposure (log10 based)
    //   2. H&D curve:    log exposure → density (per-channel characteristic curve)
    //   3. Scan encode:  density → working space code value (linear rescale)
    //
    // Each emulsion layer (R, G, B) is a physically independent coating with
    // its own characteristic curve. The green channel serves as the luminance
    // reference — red and blue diverge from green to produce the stock's
    // colour signature.
    //
    // CHANNEL CURVE PARAMETERS
    // ─────────────────────────────────────────────────────────────────────────────
    // All input values are in CAMERA STOPS relative to middle grey (0 = 0.18).
    // This matches the x-axis of published sensitometric curves directly.
    //
    //   toeStartStops  — where the curve lifts off base fog
    //   toeEndStops    — where toe transitions to straight line
    //   shoulderStops  — where straight line transitions to shoulder
    //   clipStops      — where the curve reaches Dmax (full saturation)
    //
    // Density values are ABSOLUTE STATUS-M densities as published:
    //   dMin           — base fog + minimum density
    //   dMax           — maximum achievable density
    //
    // Shape:
    //   gamma          — slope of the straight-line region (density per stop)
    //
    // SCAN ENCODING
    // ─────────────────────────────────────────────────────────────────────────────
    // After the H&D curve produces a density value, the scan encoder maps
    // the density range [dMin, dMax] into working space code values.
    // This replaces the broken fromLinear() inverse CST — density is not
    // scene-linear radiance and cannot be converted with a radiometric
    // transfer function.
    //
    // The scan encode anchors (codeBlack, codeWhite) are set per color space
    // at runtime — they are properties of the encoding, not the film.
    //
    // FILM COLOR CONTROL
    // ─────────────────────────────────────────────────────────────────────────────
    // filmColor [0,1] blends R and B density toward G density:
    //   0.0 = all channels use green curve (pure tone, no color shift)
    //   1.0 = each channel uses its own curve (full stock colour signature)
    //   >1.0 = exaggerated colour separation (artistic override)

    // Per-channel characteristic curve parameters.
    // One instance per emulsion layer (R, G, B).
    struct ChannelCurve {
        // Input — camera stops relative to middle grey (0.18 = 0 stops)
        float toeStartStops  = -6.0f;   // curve lifts off base fog
        float toeEndStops    = -3.0f;   // toe → straight transition
        float shoulderStops  =  6.0f;   // straight → shoulder transition
        float clipStops      =  8.0f;   // curve reaches Dmax

        // Density — absolute Status-M values from published data
        float dMin           =  0.20f;  // base fog
        float dMax           =  2.40f;  // maximum density

        // Shape
        float gamma          =  0.25f;  // density per stop in straight-line region
    };

    struct ToneParams {
        // Per-channel H&D curves — green is the luminance reference
        ChannelCurve red;
        ChannelCurve green;
        ChannelCurve blue;

        // Film Color — blends R/B toward G (0 = tone only, 1 = full stock colour)
        float filmColor = 1.0f;
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