// src/MasterFilmPlugin.cpp
// OFX entry points and plugin factory for MasterFilm.
// Registers both Pro and Lite plugin variants from a single binary.

#include "MasterFilmPlugin.h"
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxPixels.h"

#include "processors/ToneProcessor.h"
#include "processors/ColorProcessor.h"
#include "processors/HalationProcessor.h"
#include "processors/GrainProcessor.h"
#include "processors/AcutanceProcessor.h"
#include "presets/StockLibrary.h"
#include "presets/FilmPreset.h"

#include <cstring>
#include <stdexcept>
#include <vector>

// ── Forward declarations ──────────────────────────────────────────────────────
static OfxStatus pluginMain(const char* action,
    const void* handle,
    OfxPropertySetHandle inArgs,
    OfxPropertySetHandle outArgs);

static void setHostFunc(OfxHost* host)
{
    gHost = host;
}

// ── Global host suite pointers ────────────────────────────────────────────────
OfxHost* gHost = nullptr;
OfxPropertySuiteV1* gPropSuite = nullptr;
OfxImageEffectSuiteV1* gEffectSuite = nullptr;
OfxParameterSuiteV1* gParamSuite = nullptr;
OfxMemorySuiteV1* gMemorySuite = nullptr;
OfxMultiThreadSuiteV1* gThreadSuite = nullptr;
OfxMessageSuiteV2* gMessageSuite = nullptr;

// ── Parameter name constants ──────────────────────────────────────────────────
static constexpr const char* kParamColorSpace  = "colorSpace";

static constexpr const char* kParamToneMix     = "toneMix";
static constexpr const char* kParamFilmColor   = "filmColor";
static constexpr const char* kParamPrintGamma  = "printGamma";
static constexpr const char* kParamOrangeMask  = "orangeMask";
static constexpr const char* kParamHalationMix = "halationMix";
static constexpr const char* kParamGrainMix    = "grainMix";
static constexpr const char* kParamAcutanceMix = "acutanceMix";

// Choice indices — must match option order in onDescribeInContext
// and correspond to ColorSpaceMode enum values in FilmPreset.h.
// Rec.709 is intentionally excluded — see ColorSpaceMode for rationale.
static constexpr int kColorSpaceACEScct = 0;
static constexpr int kColorSpaceDWG = 1;

// ── Plugin descriptors ────────────────────────────────────────────────────────
static OfxPlugin gPlugins[] = {
#ifdef MASTERFILM_BUILD_PRO
    {
        kOfxImageEffectPluginApi, 1,
        "com.yourname.MasterFilmPro",
        MASTERFILM_VERSION_MAJOR, MASTERFILM_VERSION_MINOR,
        setHostFunc, pluginMain
    },
#endif
#ifdef MASTERFILM_BUILD_LITE
    {
        kOfxImageEffectPluginApi, 1,
        "com.yourname.MasterFilmLite",
        MASTERFILM_VERSION_MAJOR, MASTERFILM_VERSION_MINOR,
        setHostFunc, pluginMain
    },
#endif
};

static constexpr int kNumPlugins = static_cast<int>(sizeof(gPlugins) / sizeof(gPlugins[0]));

// ── OFX exported entry points ─────────────────────────────────────────────────

OfxExport int OfxGetNumberOfPlugins(void)
{
    return kNumPlugins;
}

OfxExport OfxPlugin* OfxGetPlugin(int nth)
{
    if (nth < 0 || nth >= kNumPlugins)
        return nullptr;
    return &gPlugins[nth];
}

// ── Suite fetch ───────────────────────────────────────────────────────────────

static OfxStatus fetchSuites()
{
    if (!gHost) return kOfxStatErrMissingHostFeature;

    gPropSuite = reinterpret_cast<OfxPropertySuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1)));
    gEffectSuite = reinterpret_cast<OfxImageEffectSuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1)));
    gParamSuite = reinterpret_cast<OfxParameterSuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1)));
    gMemorySuite = reinterpret_cast<OfxMemorySuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxMemorySuite, 1)));
    gThreadSuite = reinterpret_cast<OfxMultiThreadSuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxMultiThreadSuite, 1)));

    if (!gPropSuite || !gEffectSuite || !gParamSuite)
        return kOfxStatErrMissingHostFeature;

    return kOfxStatOK;
}

// ── Actions ───────────────────────────────────────────────────────────────────

static OfxStatus onLoad()
{
    return fetchSuites();
}

static OfxStatus onUnload()
{
    return kOfxStatOK;
}

static OfxStatus onDescribe(OfxImageEffectHandle descriptor)
{
    if (!gPropSuite || !gEffectSuite) return kOfxStatErrMissingHostFeature;

    OfxPropertySetHandle effectProps;
    gEffectSuite->getPropertySet(descriptor, &effectProps);
    gPropSuite->propSetString(effectProps, kOfxPropLabel, 0, "MasterFilm");
    gPropSuite->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, "Film");
    gPropSuite->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gPropSuite->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);

    return kOfxStatOK;
}

static OfxStatus onDescribeInContext(OfxImageEffectHandle descriptor,
    OfxPropertySetHandle inArgs)
{
    if (!gEffectSuite || !gPropSuite || !gParamSuite) return kOfxStatErrMissingHostFeature;
    (void)inArgs;

    // ── Clips ─────────────────────────────────────────────────────────────────
    OfxPropertySetHandle clipProps;
    gEffectSuite->clipDefine(descriptor, kOfxImageEffectSimpleSourceClipName, &clipProps);
    gPropSuite->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

    gEffectSuite->clipDefine(descriptor, kOfxImageEffectOutputClipName, &clipProps);
    gPropSuite->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

    // ── Parameters ────────────────────────────────────────────────────────────
    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(descriptor, &paramSet);

    // Color space dropdown — ACEScct and DaVinci Wide Gamut only.
    // MasterFilm requires scene-referred wide gamut input.
    // Rec.709 users should use external CST nodes.
    OfxPropertySetHandle paramProps;
    gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice, kParamColorSpace, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Color Space");
    gPropSuite->propSetInt(paramProps, kOfxParamPropDefault, 0, kColorSpaceACEScct);
    gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, kColorSpaceACEScct, "ACEScct");
    gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, kColorSpaceDWG, "DaVinci Wide Gamut");

    // ── Tone group ────────────────────────────────────────────────────────────
    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamToneMix, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Tone Mix");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 1.0);

    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamFilmColor, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Film Color");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 2.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 2.0);

    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamPrintGamma, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Print Contrast");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.8);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.5);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 3.5);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.5);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 3.5);

    // ── Color group ───────────────────────────────────────────────────────────
    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamOrangeMask, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Orange Mask");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 1.0);

    // ── Halation group ────────────────────────────────────────────────────────
    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamHalationMix, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Halation");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 1.0);

    // ── Grain group ───────────────────────────────────────────────────────────
    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamGrainMix, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Grain");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 1.0);

    // ── Acutance group ────────────────────────────────────────────────────────
    gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamAcutanceMix, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Acutance");
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDefault,    0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMin,        0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropMax,        0, 1.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    gPropSuite->propSetDouble(paramProps, kOfxParamPropDisplayMax, 0, 1.0);

    return kOfxStatOK;
}

static OfxStatus onCreateInstance(OfxImageEffectHandle instance)
{
    (void)instance;
    return kOfxStatOK;
}

static OfxStatus onDestroyInstance(OfxImageEffectHandle instance)
{
    (void)instance;
    return kOfxStatOK;
}

static OfxStatus onRender(OfxImageEffectHandle instance,
    OfxPropertySetHandle inArgs)
{
    if (!gEffectSuite || !gPropSuite || !gParamSuite) return kOfxStatErrMissingHostFeature;

    // ── Read color space param ────────────────────────────────────────────────
    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(instance, &paramSet);

    OfxParamHandle colorSpaceParam = nullptr;
    gParamSuite->paramGetHandle(paramSet, kParamColorSpace, &colorSpaceParam, nullptr);

    int colorSpaceChoice = kColorSpaceACEScct;
    if (colorSpaceParam)
        gParamSuite->paramGetValue(colorSpaceParam, &colorSpaceChoice);

    MasterFilm::ColorSpaceMode colorSpaceMode = MasterFilm::ColorSpaceMode::ACEScct;
    switch (colorSpaceChoice) {
    case kColorSpaceACEScct: colorSpaceMode = MasterFilm::ColorSpaceMode::ACEScct;          break;
    case kColorSpaceDWG:     colorSpaceMode = MasterFilm::ColorSpaceMode::DaVinciWideGamut; break;
    default:                 colorSpaceMode = MasterFilm::ColorSpaceMode::ACEScct;          break;
    }

    // ── Read mix/intensity params ─────────────────────────────────────────────
    auto readDoubleParam = [&](const char* name, double fallback) -> double {
        OfxParamHandle h = nullptr;
        double v = fallback;
        gParamSuite->paramGetHandle(paramSet, name, &h, nullptr);
        if (h) gParamSuite->paramGetValue(h, &v);
        return v;
    };

    const double toneMix     = readDoubleParam(kParamToneMix,     1.0);
    const double filmColor   = readDoubleParam(kParamFilmColor,   1.0);
    const double printGamma  = readDoubleParam(kParamPrintGamma,  1.8);
    const double orangeMask  = readDoubleParam(kParamOrangeMask,  0.0);
    const double halationMix = readDoubleParam(kParamHalationMix, 1.0);
    const double grainMix    = readDoubleParam(kParamGrainMix,    1.0);
    const double acutanceMix = readDoubleParam(kParamAcutanceMix, 1.0);

    // ── Get render window ─────────────────────────────────────────────────────
    OfxRectI renderWindow;
    gPropSuite->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 0, &renderWindow.x1);
    gPropSuite->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 1, &renderWindow.y1);
    gPropSuite->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 2, &renderWindow.x2);
    gPropSuite->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 3, &renderWindow.y2);

    // ── Fetch clip handles ────────────────────────────────────────────────────
    OfxImageClipHandle srcClip = nullptr;
    OfxImageClipHandle dstClip = nullptr;
    gEffectSuite->clipGetHandle(instance, kOfxImageEffectSimpleSourceClipName, &srcClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kOfxImageEffectOutputClipName, &dstClip, nullptr);
    if (!srcClip || !dstClip) return kOfxStatFailed;

    // ── Get render time ───────────────────────────────────────────────────────
    double renderTime = 0.0;
    gPropSuite->propGetDouble(inArgs, kOfxPropTime, 0, &renderTime);

    // ── Fetch image buffers ───────────────────────────────────────────────────
    OfxPropertySetHandle srcImg = nullptr;
    OfxPropertySetHandle dstImg = nullptr;
    gEffectSuite->clipGetImage(srcClip, renderTime, nullptr, &srcImg);
    gEffectSuite->clipGetImage(dstClip, renderTime, nullptr, &dstImg);

    if (!srcImg || !dstImg) {
        if (srcImg) gEffectSuite->clipReleaseImage(srcImg);
        if (dstImg) gEffectSuite->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    // ── Pixel pointers and strides ────────────────────────────────────────────
    void* srcPtr = nullptr;
    void* dstPtr = nullptr;
    gPropSuite->propGetPointer(srcImg, kOfxImagePropData, 0, &srcPtr);
    gPropSuite->propGetPointer(dstImg, kOfxImagePropData, 0, &dstPtr);

    int srcRowBytes = 0, dstRowBytes = 0;
    gPropSuite->propGetInt(srcImg, kOfxImagePropRowBytes, 0, &srcRowBytes);
    gPropSuite->propGetInt(dstImg, kOfxImagePropRowBytes, 0, &dstRowBytes);

    OfxRectI srcBounds, dstBounds;
    gPropSuite->propGetInt(srcImg, kOfxImagePropBounds, 0, &srcBounds.x1);
    gPropSuite->propGetInt(srcImg, kOfxImagePropBounds, 1, &srcBounds.y1);
    gPropSuite->propGetInt(srcImg, kOfxImagePropBounds, 2, &srcBounds.x2);
    gPropSuite->propGetInt(srcImg, kOfxImagePropBounds, 3, &srcBounds.y2);
    gPropSuite->propGetInt(dstImg, kOfxImagePropBounds, 0, &dstBounds.x1);
    gPropSuite->propGetInt(dstImg, kOfxImagePropBounds, 1, &dstBounds.y1);
    gPropSuite->propGetInt(dstImg, kOfxImagePropBounds, 2, &dstBounds.x2);
    gPropSuite->propGetInt(dstImg, kOfxImagePropBounds, 3, &dstBounds.y2);

    if (!srcPtr || !dstPtr) {
        gEffectSuite->clipReleaseImage(srcImg);
        gEffectSuite->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    // ── Preset ────────────────────────────────────────────────────────────────
    const MasterFilm::FilmPreset* preset =
        MasterFilm::StockLibrary::instance().findById("kodak_vision3_500t");

    if (!preset) {
        gEffectSuite->clipReleaseImage(srcImg);
        gEffectSuite->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    // ── Full-frame dimensions ─────────────────────────────────────────────────
    static constexpr int kNComp = 4;
    static constexpr int kBytesPerPixel = kNComp * sizeof(float);

    const int width  = renderWindow.x2 - renderWindow.x1;
    const int height = renderWindow.y2 - renderWindow.y1;
    const int nPixels = width * height;

    // ── Ping-pong buffers (full frame, contiguous) ────────────────────────────
    std::vector<float> bufA(static_cast<size_t>(nPixels * kNComp));
    std::vector<float> bufB(static_cast<size_t>(nPixels * kNComp));

    // ── Copy source image into bufA (destriding) ──────────────────────────────
    for (int y = 0; y < height; ++y)
    {
        const float* srcRow = reinterpret_cast<const float*>(
            static_cast<const char*>(srcPtr)
            + static_cast<ptrdiff_t>(renderWindow.y1 + y - srcBounds.y1) * srcRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - srcBounds.x1) * kBytesPerPixel);

        std::memcpy(&bufA[static_cast<size_t>(y * width * kNComp)],
                    srcRow,
                    static_cast<size_t>(width * kBytesPerPixel));
    }

    // ── Apply UI params to preset copies ──────────────────────────────────────
    MasterFilm::ToneParams toneParams = preset->tone;
    toneParams.filmColor  = static_cast<float>(filmColor);
    toneParams.printGamma = static_cast<float>(printGamma);

    MasterFilm::ColorParams colorParams = preset->color;
    colorParams.orangeMask = (orangeMask > 0.5);

    MasterFilm::HalationParams halationParams = preset->halation;
    halationParams.intensity *= static_cast<float>(halationMix);

    MasterFilm::GrainParams grainParams = preset->grain;
    grainParams.amount *= static_cast<float>(grainMix);

    MasterFilm::AcutanceParams acutanceParams = preset->acutance;
    acutanceParams.intensity *= static_cast<float>(acutanceMix);

    const int bufSize = nPixels * kNComp;

    // ── Render pipeline: Tone → Color → Halation → Grain → Acutance ──────────

    // Tone: bufA → bufB
    MasterFilm::ToneProcessor toneProc(toneParams);
    toneProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp, colorSpaceMode);

    // Tone mix: blend original (bufA) and toned (bufB) by toneMix
    if (toneMix < 1.0) {
        const float tm = static_cast<float>(toneMix);
        for (int i = 0; i < bufSize; ++i)
            bufB[i] = bufA[i] + tm * (bufB[i] - bufA[i]);
    }

    // Color: bufB → bufA
    MasterFilm::ColorProcessor colorProc(colorParams);
    colorProc.buildOrangeMaskLUT(toneParams, toneParams.printGamma, colorSpaceMode);
    colorProc.processCPU(bufB.data(), bufA.data(), width, height, kNComp);

    // Halation: bufA → bufB (bypass when mix=0)
    if (halationMix > 0.001) {
        MasterFilm::HalationProcessor halationProc(halationParams);
        halationProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    } else {
        std::memcpy(bufB.data(), bufA.data(), static_cast<size_t>(bufSize) * sizeof(float));
    }

    // Grain: bufB → bufA (bypass when mix=0)
    const int renderSeed = static_cast<int>(renderTime * 24.0);
    if (grainMix > 0.001) {
        MasterFilm::GrainProcessor grainProc(grainParams);
        grainProc.processCPU(bufB.data(), bufA.data(), width, height, kNComp, renderSeed);
    } else {
        std::memcpy(bufA.data(), bufB.data(), static_cast<size_t>(bufSize) * sizeof(float));
    }

    // Acutance: bufA → bufB (bypass when mix=0)
    if (acutanceMix > 0.001) {
        MasterFilm::AcutanceProcessor acutanceProc(acutanceParams);
        acutanceProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    } else {
        std::memcpy(bufB.data(), bufA.data(), static_cast<size_t>(bufSize) * sizeof(float));
    }

    // ── Copy bufB (final result) to destination image (re-striding) ───────────
    for (int y = 0; y < height; ++y)
    {
        float* dstRow = reinterpret_cast<float*>(
            static_cast<char*>(dstPtr)
            + static_cast<ptrdiff_t>(renderWindow.y1 + y - dstBounds.y1) * dstRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - dstBounds.x1) * kBytesPerPixel);

        std::memcpy(dstRow,
                    &bufB[static_cast<size_t>(y * width * kNComp)],
                    static_cast<size_t>(width * kBytesPerPixel));
    }

    // ── Release ───────────────────────────────────────────────────────────────
    gEffectSuite->clipReleaseImage(srcImg);
    gEffectSuite->clipReleaseImage(dstImg);

    return kOfxStatOK;
}

static OfxStatus pluginMain(const char* action,
    const void* handle,
    OfxPropertySetHandle inArgs,
    OfxPropertySetHandle outArgs)
{
    (void)outArgs;

    try {
        if (std::strcmp(action, kOfxActionLoad) == 0)
            return onLoad();
        if (std::strcmp(action, kOfxActionUnload) == 0)
            return onUnload();
        if (std::strcmp(action, kOfxActionDescribe) == 0)
            return onDescribe(reinterpret_cast<OfxImageEffectHandle>(
                const_cast<void*>(handle)));
        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
            return onDescribeInContext(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                inArgs);
        if (std::strcmp(action, kOfxActionCreateInstance) == 0)
            return onCreateInstance(reinterpret_cast<OfxImageEffectHandle>(
                const_cast<void*>(handle)));
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0)
            return onDestroyInstance(reinterpret_cast<OfxImageEffectHandle>(
                const_cast<void*>(handle)));
        if (std::strcmp(action, kOfxImageEffectActionRender) == 0)
            return onRender(reinterpret_cast<OfxImageEffectHandle>(
                const_cast<void*>(handle)), inArgs);

        return kOfxStatReplyDefault;
    }
    catch (const std::exception& e) {
        (void)e;
        return kOfxStatFailed;
    }
}
