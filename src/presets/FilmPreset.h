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
        DaVinciWideGamut  // DaVinci Wide Gamut / DaVinci Intermediate — middle grey at ~0.336
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

    // ── Timing / printer lights ─────────────────────────────────────────────────
    //
    // Models the photochemical printer's per-channel exposure control.
    // In the lab, printer lights adjust the exposure of each colour channel
    // independently when contact-printing the negative onto the print stock.
    // This is the colourist's primary grading tool in the photochemical chain.
    //
    // Kodak scale: 1-50, where 25 = neutral (no exposure offset).
    // Each unit ≈ 1/12 stop. The conversion is:
    //   stop_offset = (light - 25) / 12.0
    //
    // In the pipeline, printer lights are applied as stop offsets to the
    // density values coming out of the negative curve, BEFORE the print
    // stage amplifies them. This matches the physical placement: printer
    // lights modify the exposure hitting the print stock.
    struct TimingParams {
        float printerLightR = 25.0f;  // neutral = 25 (Kodak scale)
        float printerLightG = 25.0f;
        float printerLightB = 25.0f;
    };

    // ── Print stock ──────────────────────────────────────────────────────────────
    //
    // Models the print stock's contribution to the final image.
    //
    // PRINT GAMMA
    // ─────────────────────────────────────────────────────────────────────────────
    // The print stock's characteristic curve is approximately linear through
    // its straight-line region. The slope (gamma) directly scales the
    // negative's density differences:
    //   stops_out = -(densityDelta) * log2(10) * printGamma
    //
    // Phase 2 will replace this linear multiplier with the actual 2383
    // characteristic curve (baked as a LUT) for a proper composable print
    // stage with its own toe and shoulder.
    //
    // SPECTRAL MATRIX
    // ─────────────────────────────────────────────────────────────────────────────
    // The 3×3 spectral coupling matrix models the print stock's dye-layer
    // cross-talk. Each dye layer in the print stock absorbs some light from
    // adjacent spectral bands, creating inter-channel coupling.
    //
    // At identity, no cross-talk occurs. Per-stock matrices can be derived
    // from published SMPTE dye-density data for the print stock.
    //
    // Applied in density space (between negative curve and exit ramp) so
    // that the matrix operates on physically meaningful values.
    struct PrintParams {
        // Print gamma — used in LINEAR mode only (no print curve).
        // Straight-line slope of the print stock's transfer function.
        // stopsOut = rawStops × printGamma. Ignored when usePrintCurve = true.
        float printGamma = 1.8f;

        // Spectral coupling matrix — applied in density space.
        // Identity = no cross-channel coupling.
        std::array<float, 9> spectralMatrix = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };

        // ── 2383 print curve simulation ──────────────────────────────────
        //
        // When enabled, replaces the linear printGamma multiplier with
        // per-channel H&D sigmoid curves modelling the Kodak 2383 print
        // stock's three dye layers (cyan, magenta, yellow).
        //
        // Each channel uses a sigmoid: evaluateCurve(rawStops, printCurve)
        // which returns print density. The density is then converted to
        // output via Beer-Lambert (same as the negative exit ramp).
        //
        // The per-channel curves produce natural colour separation from
        // the print stock — each dye layer has its own gamma, dMax, and
        // operating-point offset, creating subtle colour shifts in shadows
        // and highlights that are characteristic of projected 2383 prints.
        //
        // Parameters are in rawStops domain (log₂, relative to mid grey).
        // Derived from published Kodak 2383 sensitometric data:
        //   - F002_1254AC: H&D curves (ECP-2D, Status A densitometry)
        //   - gamma converted: gamma_stops = gamma_logE / log₂(10)
        //   - x0 computed so that aim density falls at rawStops = 0
        //     (LAD aim: R=1.09, G=1.06, B=1.03 Status A)
        //
        // When disabled, falls back to: stopsOut = rawStops × printGamma.
        bool usePrintCurve = false;

        // Per-channel print H&D curves (used when usePrintCurve = true)
        // Red / cyan dye layer — lowest dMax, slightly steeper at operating point
        ChannelCurve printRed   = { 0.06f, 3.60f, 0.753f, -1.05f };
        // Green / magenta dye layer — reference channel
        ChannelCurve printGreen = { 0.06f, 3.90f, 0.753f, -1.33f };
        // Blue / yellow dye layer — highest dMax, steepest gamma
        ChannelCurve printBlue  = { 0.06f, 4.20f, 0.813f, -1.51f };
    };

    // ── Color / zone-weighted artistic controls ──────────────────────────────────
    //
    // Artistic colour adjustments applied AFTER the photochemical model.
    // These are creative controls, not physical film properties:
    //   - Inter-layer coupling matrix (from SMPTE density data)
    //   - Zone-weighted hue shifts and saturation scaling
    //
    // The coupling matrix is applied in density space inside the unified
    // processor, not in encoded RGB space. It models inter-layer dye
    // coupling in the negative film — a physically correct placement.
    struct ColorParams {
        // Inter-layer coupling matrix — models negative film dye cross-talk.
        // Applied in density space (inside the unified processor, between
        // the H&D curve and the exit ramp).
        // Identity = no coupling. Non-identity values from SMPTE dye data.
        std::array<float, 9> couplingMatrix = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };

        // Zone-weighted hue shifts (degrees) and saturation scaling.
        // Applied in encoded output space as artistic post-processing.
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
        TimingParams   timing;
        PrintParams    print;
        ColorParams    color;

        std::string closestStockId;
        float       closestStockConfidence = 0.0f;
    };

} // namespace MasterFilm