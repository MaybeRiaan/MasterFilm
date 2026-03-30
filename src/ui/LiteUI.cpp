// src/ui/LiteUI.cpp
#include "LiteUI.h"
#include "../MasterFilmPlugin.h"
#include <algorithm>
#include <cmath>

namespace MasterFilm {
namespace LiteUI {

OfxStatus defineParameters(OfxImageEffectHandle descriptor)
{
    if (!gParamSuite || !gEffectSuite) return kOfxStatErrMissingHostFeature;

    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(descriptor, &paramSet);

    auto def = [&](const char* name, const char* label, const char* hint) {
        OfxPropertySetHandle props;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, name, &props);
        gPropSuite->propSetDouble(props, kOfxParamPropMin,     0, 0.0);
        gPropSuite->propSetDouble(props, kOfxParamPropMax,     0, 1.0);
        gPropSuite->propSetDouble(props, kOfxParamPropDefault, 0, 0.5);
        gPropSuite->propSetString(props, kOfxPropLabel,        0, label);
        gPropSuite->propSetString(props, kOfxParamPropHint,    0, hint);
        gPropSuite->propSetInt(props,    kOfxParamPropAnimates, 0, 0);
    };

    def(kLiteGrain,     "Grain",         "Film grain intensity and character.");
    def(kLiteGlow,      "Glow",          "Bright-area halation / light bleed.");
    def(kLiteSharpness, "Sharpness",     "Edge definition / film acutance.");
    def(kLiteTone,      "Tone Curve",    "Overall contrast (toe + shoulder shape).");
    def(kLiteColor,     "Color Richness","Saturation and inter-layer color character.");

    return kOfxStatOK;
}

FilmPreset toFilmPreset(double grain, double glow, double sharpness,
                        double tone, double color,
                        const FilmPreset& base)
{
    FilmPreset p = base;  // Start from reference stock

    // ── Grain: scale amount + roughness from slider ───────────────────────────
    p.grain.amount    = static_cast<float>(grain);
    p.grain.roughness = static_cast<float>(0.3 + grain * 0.4);  // More roughness at high grain

    // ── Glow (halation): intensity + radius ───────────────────────────────────
    p.halation.intensity = static_cast<float>(glow * 0.6);
    p.halation.radius    = static_cast<float>(0.2 + glow * 0.4);

    // ── Sharpness → acutance ──────────────────────────────────────────────────
    // Low sharpness → Soft character, high → Enhanced
    if (sharpness < 0.35) {
        p.acutance.character = AcutanceCharacter::Soft;
        p.acutance.intensity = static_cast<float>(sharpness * 1.5);
    } else if (sharpness < 0.7) {
        p.acutance.character = AcutanceCharacter::Natural;
        p.acutance.intensity = static_cast<float>(sharpness);
    } else {
        p.acutance.character         = AcutanceCharacter::Enhanced;
        p.acutance.intensity         = static_cast<float>(sharpness);
        p.acutance.kostinskyStrength = static_cast<float>((sharpness - 0.7) * 0.5);
    }

    // ── Tone: contract/expand contrast around midpoint ────────────────────────
    float contrast = static_cast<float>((tone - 0.5) * 0.4);

    auto adjustTone = [&](ToneParams& t, const ToneParams& b) {
        t.toe = std::clamp(b.toe + contrast * 0.5f, 0.05f, 0.45f);
        t.shoulder = std::clamp(b.shoulder - contrast * 0.5f, 0.55f, 0.95f);
        };

    adjustTone(p.tone.acesCCT, base.tone.acesCCT);
    adjustTone(p.tone.dwg, base.tone.dwg);
    adjustTone(p.tone.rec709, base.tone.rec709);

    // ── Color richness → saturation across all zones ──────────────────────────
    float satScale = static_cast<float>(0.5 + color * 1.0);  // [0.5, 1.5]
    p.color.satShadow    *= satScale;
    p.color.satMid       *= satScale;
    p.color.satHighlight *= satScale;

    return p;
}

} // namespace LiteUI
} // namespace MasterFilm
