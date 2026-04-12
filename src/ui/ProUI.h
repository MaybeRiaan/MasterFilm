// src/ui/ProUI.h
// Full parameter control surface for MasterFilm Pro.
// Describes all OFX parameters into the host — five characteristic sections.
#pragma once

#include "ofxImageEffect.h"
#include "../presets/FilmPreset.h"

namespace MasterFilm {
namespace ProUI {

// Parameter name constants (used as OFX param identifiers)
// ── Grain (FilmStockProfile) ──────────────────────────────────────────────────
static constexpr const char* kGrainIntensity       = "grainIntensity";
static constexpr const char* kGrainSize            = "grainSize";
static constexpr const char* kGrainColorNoise      = "grainColorNoise";
static constexpr const char* kGrainMorphology      = "grainMorphology";
static constexpr const char* kGrainClumping        = "grainClumping";

// ── Halation ──────────────────────────────────────────────────────────────────
static constexpr const char* kHalationIntensity  = "halationIntensity";
static constexpr const char* kHalationRadius     = "halationRadius";
static constexpr const char* kHalationThreshold  = "halationThreshold";
static constexpr const char* kHalationBiasR      = "halationBiasR";
static constexpr const char* kHalationBiasG      = "halationBiasG";
static constexpr const char* kHalationBiasB      = "halationBiasB";

// ── Acutance ──────────────────────────────────────────────────────────────────
static constexpr const char* kAcutanceCharacter  = "acutanceCharacter";
static constexpr const char* kAcutanceIntensity  = "acutanceIntensity";
static constexpr const char* kAcutanceRolloff    = "acutanceRolloff";
static constexpr const char* kKostinsky          = "kostinskyStrength";

// ── Tone ──────────────────────────────────────────────────────────────────────
static constexpr const char* kToneBlack    = "toneBlackPoint";
static constexpr const char* kToneWhite    = "toneWhitePoint";
static constexpr const char* kToneToe      = "toneToe";
static constexpr const char* kToneShoulder = "toneShoulder";
static constexpr const char* kToneGamma    = "toneMidGamma";

// ── Color ──────────────────────────────────────────────────────────────────────
static constexpr const char* kHueShadow      = "colorHueShadow";
static constexpr const char* kHueMid         = "colorHueMid";
static constexpr const char* kHueHighlight   = "colorHueHighlight";
static constexpr const char* kSatShadow      = "colorSatShadow";
static constexpr const char* kSatMid         = "colorSatMid";
static constexpr const char* kSatHighlight   = "colorSatHighlight";

// ── Preset ────────────────────────────────────────────────────────────────────
static constexpr const char* kPresetChoice   = "presetChoice";

// Define all parameters into the OFX descriptor
OfxStatus defineParameters(OfxImageEffectHandle descriptor);

// Read current param values from a plugin instance into a FilmPreset struct
FilmPreset readParams(OfxImageEffectHandle instance);

// Push a preset's values back into the OFX params
OfxStatus applyPreset(OfxImageEffectHandle instance, const FilmPreset& preset);

} // namespace ProUI
} // namespace MasterFilm
