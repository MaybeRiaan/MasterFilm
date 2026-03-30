// src/ui/LiteUI.h
// Simplified five-slider surface for MasterFilm Lite.
// Perceptual labels — no film science knowledge required.
// Internally maps to the same Pro engine via LiteUI::toFilmPreset().
#pragma once

#include "ofxImageEffect.h"
#include "../presets/FilmPreset.h"

namespace MasterFilm {
namespace LiteUI {

// Five perceptual sliders — all [0,1]
static constexpr const char* kLiteGrain     = "liteGrain";      // "Grain"
static constexpr const char* kLiteGlow      = "liteGlow";       // "Glow" (halation)
static constexpr const char* kLiteSharpness = "liteSharpness";  // "Sharpness" (acutance)
static constexpr const char* kLiteTone      = "liteTone";       // "Tone Curve" (contrast)
static constexpr const char* kLiteColor     = "liteColor";      // "Color Richness" (saturation)

// Define the five Lite parameters into the OFX descriptor
OfxStatus defineParameters(OfxImageEffectHandle descriptor);

// Map Lite slider values → full FilmPreset for engine consumption
// Uses a reference preset (defaults to Kodak Vision3 500T) as the base,
// then scales the perceptual sliders over that base.
FilmPreset toFilmPreset(double grain, double glow, double sharpness,
                        double tone, double color,
                        const FilmPreset& basePreset);

} // namespace LiteUI
} // namespace MasterFilm
