// src/ui/ProUI.cpp
// OFX parameter definition for MasterFilm Pro.
// Each characteristic is defined in its own section with page/group hints.

#include "ProUI.h"
#include "../MasterFilmPlugin.h"
#include "../presets/StockLibrary.h"
#include <cstring>

namespace MasterFilm {
namespace ProUI {

// Helper: define a double param with range, default, label, hint
static OfxStatus defineDouble(OfxParamSetHandle paramSet,
                              const char* name,
                              double min, double max, double def,
                              const char* label, const char* hint)
{
    OfxPropertySetHandle props;
    OfxStatus stat = gParamSuite->paramDefine(paramSet,
                                              kOfxParamTypeDouble,
                                              name, &props);
    if (stat != kOfxStatOK) return stat;

    gPropSuite->propSetDouble(props, kOfxParamPropMin,         0, min);
    gPropSuite->propSetDouble(props, kOfxParamPropMax,         0, max);
    gPropSuite->propSetDouble(props, kOfxParamPropDefault,     0, def);
    gPropSuite->propSetString(props, kOfxPropLabel,            0, label);
    gPropSuite->propSetString(props, kOfxParamPropHint,        0, hint);
    gPropSuite->propSetInt(props,    kOfxParamPropAnimates,    0, 0);

    return kOfxStatOK;
}

OfxStatus defineParameters(OfxImageEffectHandle descriptor)
{
    if (!gParamSuite || !gEffectSuite) return kOfxStatErrMissingHostFeature;

    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(descriptor, &paramSet);

    // ── Preset choice ─────────────────────────────────────────────────────────
    {
        OfxPropertySetHandle props;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice,
                                 kPresetChoice, &props);
        gPropSuite->propSetString(props, kOfxPropLabel, 0, "Film Stock");
        gPropSuite->propSetString(props, kOfxParamPropHint, 0,
            "Select a reference film stock. Drives sliders to preset values.");

        // Populate choices from stock library
        const auto& presets = StockLibrary::instance().allPresets();
        gPropSuite->propSetInt(props, kOfxParamPropDefault, 0, 0);
        // Choice option labels are set via kOfxParamPropChoiceOption
        int i = 0;
        for (const auto& p : presets) {
            gPropSuite->propSetString(props, kOfxParamPropChoiceOption,
                                     i++, p.displayName.c_str());
        }
    }

    // ── Grain section (FilmStockProfile) ─────────────────────────────────────
    defineDouble(paramSet, kGrainIntensity,  0.0, 2.0, 1.0, "Intensity",
                 "RMS granularity multiplier (1.0 = stock reference).");
    defineDouble(paramSet, kGrainSize,       0.0, 1.0, 0.5, "Size",
                 "Grain particle size — maps to spatial frequency sigma.");
    defineDouble(paramSet, kGrainColorNoise, 0.0, 1.0, 0.35,"Color Noise",
                 "Chroma micro-contrast: 0 = monochromatic, 1 = full colour grain.");
    defineDouble(paramSet, kGrainClumping,   0.0, 1.0, 0.42,"Clumping",
                 "Spatial agglomeration — higher values produce larger grain clusters.");
    {
        OfxPropertySetHandle props;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice,
                                 kGrainMorphology, &props);
        gPropSuite->propSetString(props, kOfxPropLabel, 0, "Morphology");
        gPropSuite->propSetString(props, kOfxParamPropHint, 0,
            "Grain crystal shape. Cubic = classic. T-Grain = modern tabular.");
        gPropSuite->propSetInt(props, kOfxParamPropDefault, 0, 0);
        gPropSuite->propSetString(props, kOfxParamPropChoiceOption, 0, "Cubic");
        gPropSuite->propSetString(props, kOfxParamPropChoiceOption, 1, "T-Grain");
    }

    // ── Halation section ──────────────────────────────────────────────────────
    defineDouble(paramSet, kHalationIntensity, 0.0, 1.0, 0.3,  "Intensity",  "Overall halation strength.");
    defineDouble(paramSet, kHalationRadius,    0.0, 1.0, 0.4,  "Radius",     "Inner glow radius (% of image height).");
    defineDouble(paramSet, kHalationThreshold, 0.0, 1.0, 0.75, "Threshold",  "Luminance level above which halation activates.");
    defineDouble(paramSet, kHalationBiasR,     0.0, 1.0, 1.0,  "Red Bias",   "Red channel contribution to halation glow.");
    defineDouble(paramSet, kHalationBiasG,     0.0, 1.0, 0.3,  "Green Bias", "Green channel contribution.");
    defineDouble(paramSet, kHalationBiasB,     0.0, 1.0, 0.1,  "Blue Bias",  "Blue channel contribution.");

    // ── Acutance section ──────────────────────────────────────────────────────
    {
        OfxPropertySetHandle props;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice,
                                 kAcutanceCharacter, &props);
        gPropSuite->propSetString(props, kOfxPropLabel, 0, "Character");
        gPropSuite->propSetInt(props, kOfxParamPropDefault, 0, 1); // Natural
        gPropSuite->propSetString(props, kOfxParamPropChoiceOption, 0, "Soft");
        gPropSuite->propSetString(props, kOfxParamPropChoiceOption, 1, "Natural");
        gPropSuite->propSetString(props, kOfxParamPropChoiceOption, 2, "Enhanced");
    }
    defineDouble(paramSet, kAcutanceIntensity, 0.0, 1.0, 0.5, "Intensity", "Edge emphasis strength.");
    defineDouble(paramSet, kAcutanceRolloff,   0.0, 1.0, 0.5, "Rolloff",   "Frequency rolloff — lower = more high-frequency emphasis.");
    defineDouble(paramSet, kKostinsky,         0.0, 1.0, 0.0, "Adjacency", "Kostinsky adjacency term (Enhanced mode). Models MTF > 1.0.");

    // ── Tone section ──────────────────────────────────────────────────────────
    defineDouble(paramSet, kToneBlack,    0.0,  0.2, 0.0,  "Black Point",  "Lift shadows.");
    defineDouble(paramSet, kToneWhite,    0.8,  1.0, 1.0,  "White Point",  "Clamp highlights.");
    defineDouble(paramSet, kToneToe,      0.0,  0.5, 0.3,  "Toe",          "Shadow curve rolloff.");
    defineDouble(paramSet, kToneShoulder, 0.5,  1.0, 0.7,  "Shoulder",     "Highlight curve rolloff.");
    defineDouble(paramSet, kToneGamma,    0.5,  2.0, 1.0,  "Mid Gamma",    "Mid-tone gamma.");

    // ── Color section ─────────────────────────────────────────────────────────
    defineDouble(paramSet, kHueShadow,    -180.0, 180.0, 0.0, "Hue Shadows",    "Hue shift in shadow zone (degrees).");
    defineDouble(paramSet, kHueMid,       -180.0, 180.0, 0.0, "Hue Mids",       "Hue shift in mid-tone zone.");
    defineDouble(paramSet, kHueHighlight, -180.0, 180.0, 0.0, "Hue Highlights", "Hue shift in highlight zone.");
    defineDouble(paramSet, kSatShadow,    0.0, 2.0, 1.0, "Sat Shadows",    "Saturation scale in shadow zone.");
    defineDouble(paramSet, kSatMid,       0.0, 2.0, 1.0, "Sat Mids",       "Saturation scale in mid-tone zone.");
    defineDouble(paramSet, kSatHighlight, 0.0, 2.0, 1.0, "Sat Highlights", "Saturation scale in highlight zone.");

    return kOfxStatOK;
}

FilmPreset readParams(OfxImageEffectHandle /*instance*/)
{
    // TODO: fetch each param handle via gParamSuite->paramGetHandle(),
    //       then gParamSuite->paramGetValue() → populate FilmPreset
    FilmPreset p;
    return p;
}

OfxStatus applyPreset(OfxImageEffectHandle /*instance*/, const FilmPreset& /*preset*/)
{
    // TODO: gParamSuite->paramSetValue() for each field in preset
    return kOfxStatReplyDefault;
}

} // namespace ProUI
} // namespace MasterFilm
