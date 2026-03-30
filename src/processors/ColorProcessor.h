// src/processors/ColorProcessor.h
// Pass 1b: inter-layer coupling matrix + zone-weighted hue/saturation shifts.
// Runs in the same pass as ToneProcessor (combined shader) for efficiency.
#pragma once
#include "../presets/FilmPreset.h"
#include "ofxImageEffect.h"

namespace MasterFilm {

class ColorProcessor {
public:
    explicit ColorProcessor(const ColorParams& params) : mParams(params) {}

    OfxStatus processGPU(OfxImageEffectHandle effect,
                         OfxPropertySetHandle srcImg,
                         OfxPropertySetHandle dstImg) const;

    OfxStatus processCPU(const float* src, float* dst,
                         int width, int height, int nComponents) const;

    void setParams(const ColorParams& p) { mParams = p; }
    const ColorParams& params() const    { return mParams; }

private:
    ColorParams mParams;

    // Apply the 3×3 coupling matrix to an RGB triplet
    void applyCoupling(float& r, float& g, float& b) const;

    // Zone-weighted hue + saturation shift
    // luma: perceived luminance of the pixel [0,1]
    void applyZoneColor(float& r, float& g, float& b, float luma) const;

    // Helpers: RGB ↔ HSL
    static void rgbToHsl(float r, float g, float b,
                         float& h, float& s, float& l);
    static void hslToRgb(float h, float s, float l,
                         float& r, float& g, float& b);

    // Zone blend weight [0,1] for shadow/mid/highlight at given luma
    static float shadowBlend(float luma);
    static float midBlend(float luma);
    static float highlightBlend(float luma);
};

} // namespace MasterFilm
