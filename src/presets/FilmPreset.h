// src/presets/FilmPreset.h
// Central data structure representing a complete film stock preset.
// All five characteristic blocks live here so processors can read them
// without knowing anything about the UI or parameter system.
#pragma once

#include <string>
#include <array>

namespace MasterFilm {

// ── Grain ─────────────────────────────────────────────────────────────────────
struct GrainParams {
    float amount        = 0.5f;   // Overall grain strength [0,1]
    float size          = 0.5f;   // Grain particle size [0,1] → maps to σ in PSD model
    float roughness     = 0.5f;   // Clumping / clustering tendency [0,1]

    // Zone-weighted breakdown (must sum to ~1.0, but not enforced here)
    float shadowWeight  = 0.40f;  // Grain presence in shadows
    float midWeight     = 0.45f;  // Grain presence in mids
    float highlightWeight = 0.15f; // Grain presence in highlights

    // Internal: RMS granularity value from manufacturer spec (Kodak/Ilford scale)
    float rmsGranularity = 10.0f;
    float iso            = 400.0f;
};

// ── Halation ──────────────────────────────────────────────────────────────────
struct HalationParams {
    float intensity  = 0.3f;  // Overall halation strength [0,1]
    float radius     = 0.5f;  // Blur radius (two-Gaussian model, inner lobe) [0,1]
    float threshold  = 0.7f;  // Luminance threshold above which halation activates

    // Per-channel spectral bias (red-dominant for most stocks)
    float biasR = 1.0f;
    float biasG = 0.3f;
    float biasB = 0.1f;

    // Outer Gaussian lobe (wide glow)
    float outerRadiusScale = 4.0f;   // Multiplier on inner radius
    float outerWeight      = 0.25f;  // Relative weight of outer lobe
};

// ── Acutance ──────────────────────────────────────────────────────────────────
enum class AcutanceCharacter {
    Soft,       // T-grain / fine-grain — gentle MTF rolloff
    Natural,    // Standard cubic grain — balanced
    Enhanced    // Adjacency-effect stocks (Velvia) — MTF can exceed 1.0
};

struct AcutanceParams {
    AcutanceCharacter character = AcutanceCharacter::Natural;
    float intensity   = 0.5f;  // Edge emphasis strength [0,1]
    float rolloff     = 0.5f;  // How quickly the enhancement falls off at high freq
    // Kostinsky adjacency term (for Enhanced mode)
    float kostinskyStrength = 0.0f;
};

// ── Tone ──────────────────────────────────────────────────────────────────────
struct ToneParams {
    float blackPoint  = 0.0f;   // Lift [0, 0.2]
    float whitePoint  = 1.0f;   // Gain [0.8, 1.0]
    float toe         = 0.3f;   // Shadow rolloff [0,1]
    float shoulder    = 0.7f;   // Highlight rolloff [0,1]
    float midGamma    = 1.0f;   // Mid-tone gamma adjustment [0.5, 2.0]
};

// ── Color / Inter-layer coupling ──────────────────────────────────────────────
struct ColorParams {
    // 3×3 density-dependent inter-layer coupling matrix (row-major, identity = no coupling)
    std::array<float, 9> couplingMatrix = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };

    // Hue shift per zone (degrees, −180 to +180)
    float hueShadowShift    =  0.0f;
    float hueMidShift       =  0.0f;
    float hueHighlightShift =  0.0f;

    // Saturation scale per luminance zone [0,2]
    float satShadow    = 1.0f;
    float satMid       = 1.0f;
    float satHighlight = 1.0f;
};

// ── Complete preset ────────────────────────────────────────────────────────────
struct FilmPreset {
    std::string id;           // Unique machine ID e.g. "kodak_vision3_500t"
    std::string displayName;  // Human label e.g. "Kodak Vision3 500T"
    std::string category;     // "B&W" | "Cinema" | "Slide" | "Print" | "Custom"
    std::string notes;        // Optional provenance note shown in UI

    GrainParams    grain;
    HalationParams halation;
    AcutanceParams acutance;
    ToneParams     tone;
    ColorParams    color;

    // Closest-stock hint (phase 2 feature — populated but not used yet)
    std::string closestStockId;
    float       closestStockConfidence = 0.0f;
};

} // namespace MasterFilm
