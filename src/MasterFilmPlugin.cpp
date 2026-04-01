// src/MasterFilmPlugin.cpp
// OFX entry points and plugin factory for MasterFilm.
// Registers both Pro and Lite plugin variants from a single binary.

#include "MasterFilmPlugin.h"
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxPixels.h"

#include "processors/ToneProcessor.h"
#include "presets/StockLibrary.h"
#include "presets/FilmPreset.h"

#include <cstring>
#include <stdexcept>

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
static constexpr const char* kParamColorSpace = "colorSpace";

// Choice indices — must match option order in onDescribeInContext
// and correspond to ColorSpaceMode enum values in FilmPreset.h
static constexpr int kColorSpaceACEScct = 0;
static constexpr int kColorSpaceDWG = 1;
static constexpr int kColorSpaceRec709 = 2;

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

    // Color space dropdown — tells the plugin which internal CST to apply.
    // The tone curve always operates in scene linear; this controls the
    // forward and inverse transfer functions that wrap it.
    OfxPropertySetHandle paramProps;
    gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice, kParamColorSpace, &paramProps);
    gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Color Space");
    gPropSuite->propSetInt(paramProps, kOfxParamPropDefault, 0, kColorSpaceACEScct);
    gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, kColorSpaceACEScct, "ACEScct");
    gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, kColorSpaceDWG, "DaVinci Wide Gamut");
    gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, kColorSpaceRec709, "Rec.709");

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
    case kColorSpaceRec709:  colorSpaceMode = MasterFilm::ColorSpaceMode::Rec709;           break;
    default:                 colorSpaceMode = MasterFilm::ColorSpaceMode::ACEScct;          break;
    }

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

    // ── Build tone processor ──────────────────────────────────────────────────
    // StockLibrary is a singleton — no heap allocation per frame.
    // Hardcoded to Vision3 500T for Pass 1 validation.
    // TODO: read selected stock from param once stock picker UI is wired.
    const MasterFilm::FilmPreset* preset =
        MasterFilm::StockLibrary::instance().findById("kodak_vision3_500t");

    if (!preset) {
        gEffectSuite->clipReleaseImage(srcImg);
        gEffectSuite->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    MasterFilm::ToneProcessor toneProc(preset->tone);

    // ── Process row by row ────────────────────────────────────────────────────
    // processCPU is called once per row (height=1).
    // ColorSpaceMode is passed so the processor applies the correct
    // forward/inverse CST around the linear-light tone curve.
    static constexpr int kNComponents = 4;
    static constexpr int kBytesPerPixel = kNComponents * sizeof(float);
    const int rowWidth = renderWindow.x2 - renderWindow.x1;

    for (int y = renderWindow.y1; y < renderWindow.y2; ++y)
    {
        const float* srcRow = reinterpret_cast<const float*>(
            static_cast<const char*>(srcPtr)
            + static_cast<ptrdiff_t>(y - srcBounds.y1) * srcRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - srcBounds.x1) * kBytesPerPixel);

        float* dstRow = reinterpret_cast<float*>(
            static_cast<char*>(dstPtr)
            + static_cast<ptrdiff_t>(y - dstBounds.y1) * dstRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - dstBounds.x1) * kBytesPerPixel);

        toneProc.processCPU(srcRow, dstRow, rowWidth, 1, kNComponents, colorSpaceMode);
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