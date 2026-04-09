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
    // The tone processor models the photochemical chain:
    //
    //   1. Exposure:     scene linear → log2 stops relative to middle grey
    //   2. H&D curve:    stops → density (per-channel sigmoid characteristic curve)
    //   3. Print gamma:  density delta amplified by print stock contrast
    //   4. Exit ramp:    amplified density → Beer-Lambert transmission → re-encode
    //
    // Each emulsion layer (R, G, B) is a physically independent coating with
    // its own characteristic curve. The green channel serves as the luminance
    // reference — red and blue diverge from green to produce the stock's
    // colour signature.
    //
    // CHANNEL CURVE PARAMETERS
    // ─────────────────────────────────────────────────────────────────────────────
    // Density values are ABSOLUTE STATUS-M densities from published datasheet:
    //   dMin           — base fog + minimum density
    //   dMax           — maximum achievable density
    //
    // Shape:
    //   gamma          — slope of the straight-line region (density per stop)
    //   x0             — sigmoid inflection point = (toeEnd + shoulder) / 2
    //
    // EXIT RAMP
    // ─────────────────────────────────────────────────────────────────────────────
    // The density delta (D - dMid) is converted to output stops via
    // Beer-Lambert transmission, amplified by printGamma:
    //   stops_out = -(D - dMid) * log2(10) * printGamma
    //   lin_out   = 0.18 * 2^stops_out
    //   code_out  = fromLinear(lin_out)
    //
    // printGamma models the print stock's contrast contribution.
    // Middle grey is exactly preserved (delta=0). Phase 2 will replace
    // this linear multiplier with the actual 2383 characteristic curve.
    //
    // FILM COLOR CONTROL
    // ─────────────────────────────────────────────────────────────────────────────
    // filmColor [0,1] blends R and B density toward G density:
    //   0.0 = all channels use green curve (pure tone, no color shift)
    //   1.0 = each channel uses its own curve (full stock colour signature)
    //   >1.0 = exaggerated colour separation (artistic override)

    // Per-channel characteristic curve parameters.
    // One instance per emulsion layer (R, G, B).
    //
    // The H&D curve is modelled as a sigmoid (modified Richards curve):
    //   D = dMin + (dMax - dMin) / (1 + exp(-k * (stops - x0)))
    //
    // where:
    //   k  = 4 * gamma / (dMax - dMin)   — derived at runtime, matches straight-line slope
    //   x0 = (toeEndStops + shoulderStops) / 2  — inflection point
    //
    // This replaces the piecewise toe/straight/shoulder model which required
    // an arbitrary 0.3f junction approximation.
    struct ChannelCurve {
        // Density — absolute Status-M values from published datasheet
        float dMin = 0.20f;  // base fog density
        float dMax = 2.40f;  // maximum density (saturation)

        // Shape — straight-line slope from sensitometric chart
        float gamma = 0.25f;  // density per stop

        // Sigmoid inflection point in stops relative to middle grey.
        // Set to (toeEndStops + shoulderStops) / 2 from datasheet geometry.
        float x0 = 1.75f;  // stops — green 500T default
    };

    struct ToneParams {
        // Per-channel H&D curves — green is the luminance reference
        ChannelCurve red;
        ChannelCurve green;
        ChannelCurve blue;

        // Film Color — blends R/B toward G (0 = tone only, 1 = full stock colour)
        float filmColor = 1.0f;

        // Print gamma — models the print stock's contrast amplification.
        // The negative film's density differences are scaled by this value
        // in the exit ramp: stops_out = -densityDelta * log2(10) * printGamma.
        //
        // 1.0 = raw negative density (low contrast, shadows crushed)
        // 1.8 = default — approximates a scan-graded 2383 projection
        // 2.5 = high contrast theatrical print
        //
        // Middle grey is unaffected (delta=0 at mid). Colour character
        // comes from per-channel density differences in the negative curve,
        // which printGamma amplifies uniformly.
        //
        // Phase 2 will replace this linear multiplier with the actual 2383
        // characteristic curve for a proper composable print stage.
        float printGamma = 1.8f;
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
        bool orangeMask = false;
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