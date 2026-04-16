// src/MasterFilmPlugin.cpp
// OFX entry points and plugin factory for MasterFilm.
// Registers both Pro and Lite plugin variants from a single binary.

#include "MasterFilmPlugin.h"
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxPixels.h"

#include "processors/UnifiedFilmProcessor.h"
#include "processors/HalationProcessor.h"
#include "processors/GrainProcessor.h"
#include "processors/AcutanceProcessor.h"
#include "presets/StockLibrary.h"
#include "presets/FilmPreset.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef MASTERFILM_ENABLE_OPENGL
#include "ofxGPURender.h"
#include "platform/GLSLDispatch.h"
#include <cmath>
#include <map>
#include <mutex>
#endif

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

#ifdef MASTERFILM_ENABLE_OPENGL
static OfxImageEffectOpenGLRenderSuiteV1* gGLSuite = nullptr;
static std::map<OfxImageEffectHandle, MasterFilm::GLSLDispatch*> gGLDispatchers;
static std::mutex gGLDispatchersMutex;
static std::string gBundlePath;

static void createFloatTexAndFBO(int w, int h, GLuint& tex, GLuint& fbo)
{
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
#endif // MASTERFILM_ENABLE_OPENGL

// ── Parameter name constants ──────────────────────────────────────────────────
static constexpr const char* kParamColorSpace     = "colorSpace";

// Printer lights — per-channel exposure control (Kodak scale 1-50, 25 = neutral)
static constexpr const char* kParamPrinterLightR  = "printerLightR";
static constexpr const char* kParamPrinterLightG  = "printerLightG";
static constexpr const char* kParamPrinterLightB  = "printerLightB";

// Print curve mode — toggles between linear printGamma and 2383 S-curve
static constexpr const char* kParamUsePrintCurve  = "usePrintCurve";

// Per-pass enable checkboxes — toggled at runtime for debugging / verification.
static constexpr const char* kParamEnableTone     = "enableTone";
static constexpr const char* kParamEnableColor    = "enableColor";
static constexpr const char* kParamEnableHalation = "enableHalation";
static constexpr const char* kParamEnableGrain    = "enableGrain";
static constexpr const char* kParamEnableAcutance = "enableAcutance";
static constexpr const char* kParamEnableGPU      = "enableGPU";
static constexpr const char* kParamRenderPath     = "renderPath";

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

#ifdef MASTERFILM_ENABLE_OPENGL
    gGLSuite = reinterpret_cast<OfxImageEffectOpenGLRenderSuiteV1*>(
        const_cast<void*>(gHost->fetchSuite(gHost->host, kOfxOpenGLRenderSuite, 1)));
#endif

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

#ifdef MASTERFILM_ENABLE_OPENGL
    // "optional" tells the host we prefer GPU but can fall back to CPU if no GL context.
    // "true" would mean "required" — Resolve rejects the plugin if it can't provide GL.
    gPropSuite->propSetString(effectProps, kOfxImageEffectPropOpenGLRenderSupported, 0, "optional");
    // Capture the bundle root path (e.g. ".../MasterFilm.ofx.bundle") for shader loading.
   char* filePath = nullptr;
    gPropSuite->propGetString(effectProps, kOfxPluginPropFilePath, 0, &filePath);
    if (filePath && filePath[0] != '\0') gBundlePath = filePath;
#endif

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

    // ── Printer lights (Kodak scale: 1-50, 25 = neutral) ─────────────────────
    // Each unit ≈ 1/12 stop. Adjusts per-channel exposure on the print stock.
    // Physically equivalent to the lab colourist's primary grading control.
    {
        struct PLDef { const char* name; const char* label; const char* hint; };
        static const PLDef kPLParams[] = {
            { kParamPrinterLightR, "Printer Light R", "Red printer light (Kodak scale). 25 = neutral." },
            { kParamPrinterLightG, "Printer Light G", "Green printer light (Kodak scale). 25 = neutral." },
            { kParamPrinterLightB, "Printer Light B", "Blue printer light (Kodak scale). 25 = neutral." },
        };
        for (const auto& pl : kPLParams) {
            OfxPropertySetHandle plProps;
            gParamSuite->paramDefine(paramSet, kOfxParamTypeDouble, pl.name, &plProps);
            gPropSuite->propSetString(plProps, kOfxPropLabel,            0, pl.label);
            gPropSuite->propSetString(plProps, kOfxParamPropHint,        0, pl.hint);
            gPropSuite->propSetDouble(plProps, kOfxParamPropMin,         0, 1.0);
            gPropSuite->propSetDouble(plProps, kOfxParamPropMax,         0, 50.0);
            gPropSuite->propSetDouble(plProps, kOfxParamPropDefault,     0, 25.0);
            gPropSuite->propSetDouble(plProps, kOfxParamPropDisplayMin,  0, 22.0);
            gPropSuite->propSetDouble(plProps, kOfxParamPropDisplayMax,  0, 28.0);
            gPropSuite->propSetInt(plProps,    kOfxParamPropAnimates,    0, 0);
        }
    }

    // ── Print curve toggle ──────────────────────────────────────────────────
    {
        OfxPropertySetHandle pcProps;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeBoolean, kParamUsePrintCurve, &pcProps);
        gPropSuite->propSetString(pcProps, kOfxPropLabel,         0, "2383 Print Curve");
        gPropSuite->propSetString(pcProps, kOfxParamPropHint,     0,
            "ON: Kodak 2383 S-curve with soft toe and shoulder compression. "
            "OFF: Linear print gamma (original model).");
        gPropSuite->propSetInt(pcProps,    kOfxParamPropDefault,  0, 1);   // ON by default
        gPropSuite->propSetInt(pcProps,    kOfxParamPropAnimates, 0, 0);
    }

    // ── Per-pass enable checkboxes (for debugging / isolating passes) ─────────
    // All default ON so the plugin behaves identically to the original out of the box.
    struct BoolDef { const char* name; const char* label; const char* hint; };
    static const BoolDef kBoolParams[] = {
        { kParamEnableTone,     "Tone",      "Enable tone curve (H&D characteristic). CPU pass." },
        { kParamEnableColor,    "Color",     "Enable color coupling and zone hue/sat. CPU pass." },
        { kParamEnableHalation, "Halation",  "Enable lens halation. GPU passes 1–2 (or CPU)." },
        { kParamEnableGrain,    "Grain",     "Enable film grain. GPU pass 3 (or CPU)." },
        { kParamEnableAcutance, "Acutance",  "Enable acutance / edge emphasis. GPU pass 4 (or CPU)." },
        { kParamEnableGPU,      "Use GPU",   "Use OpenGL GPU path when the host provides a GL context. "
                                             "Uncheck to force CPU rendering for side-by-side comparison." },
    };
    for (const auto& bd : kBoolParams) {
        OfxPropertySetHandle boolProps;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeBoolean, bd.name, &boolProps);
        gPropSuite->propSetString(boolProps, kOfxPropLabel,         0, bd.label);
        gPropSuite->propSetString(boolProps, kOfxParamPropHint,     0, bd.hint);
        gPropSuite->propSetInt(boolProps,    kOfxParamPropDefault,  0, 1);   // ON by default
        gPropSuite->propSetInt(boolProps,    kOfxParamPropAnimates, 0, 0);
    }

    // ── Render path indicator (read-only, updated each frame) ────────────────
    {
        OfxPropertySetHandle rpProps;
        gParamSuite->paramDefine(paramSet, kOfxParamTypeString, kParamRenderPath, &rpProps);
        gPropSuite->propSetString(rpProps, kOfxPropLabel,              0, "Render Path");
        gPropSuite->propSetString(rpProps, kOfxParamPropHint,          0, "Shows which render path is active (GPU or CPU).");
        gPropSuite->propSetString(rpProps, kOfxParamPropDefault,       0, "---");
        gPropSuite->propSetInt(rpProps,    kOfxParamPropAnimates,      0, 0);
        gPropSuite->propSetInt(rpProps,    kOfxParamPropEvaluateOnChange, 0, 0);
        gPropSuite->propSetInt(rpProps,    kOfxParamPropEnabled,       0, 0);  // greyed out / read-only
    }

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

    // ── Read per-pass enable checkboxes ───────────────────────────────────────
    // Helper: returns true (enabled) if the param is missing (safe default).
    auto readBool = [&](const char* name) -> bool {
        OfxParamHandle h = nullptr;
        gParamSuite->paramGetHandle(paramSet, name, &h, nullptr);
        int v = 1;
        if (h) gParamSuite->paramGetValue(h, &v);
        return v != 0;
    };
    const bool enableTone     = readBool(kParamEnableTone);
    const bool enableColor    = readBool(kParamEnableColor);
    const bool enableHalation = readBool(kParamEnableHalation);
    const bool enableGrain    = readBool(kParamEnableGrain);
    const bool enableAcutance = readBool(kParamEnableAcutance);
    const bool enableGPU      = readBool(kParamEnableGPU);
    const bool usePrintCurve  = readBool(kParamUsePrintCurve);

    // Helper: update the render path indicator string visible in the UI.
    auto setRenderPathLabel = [&](const char* label) {
        OfxParamHandle h = nullptr;
        gParamSuite->paramGetHandle(paramSet, kParamRenderPath, &h, nullptr);
        if (h) gParamSuite->paramSetValue(h, label);
    };

    // ── Read printer lights ──────────────────────────────────────────────────
    auto readDouble = [&](const char* name, double fallback) -> double {
        OfxParamHandle h = nullptr;
        gParamSuite->paramGetHandle(paramSet, name, &h, nullptr);
        double v = fallback;
        if (h) gParamSuite->paramGetValue(h, &v);
        return v;
    };
    const double printerLightR = readDouble(kParamPrinterLightR, 25.0);
    const double printerLightG = readDouble(kParamPrinterLightG, 25.0);
    const double printerLightB = readDouble(kParamPrinterLightB, 25.0);

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

    // ── Preset ────────────────────────────────────────────────────────────────
    const MasterFilm::FilmPreset* preset =
        MasterFilm::StockLibrary::instance().findById("kodak_vision3_500t");
    if (!preset) return kOfxStatFailed;

    // ── Full-frame dimensions ─────────────────────────────────────────────────
    static constexpr int kNComp = 4;
    static constexpr int kBytesPerPixel = kNComp * sizeof(float);

    const int width   = renderWindow.x2 - renderWindow.x1;
    const int height  = renderWindow.y2 - renderWindow.y1;
    const int nPixels = width * height;

    // ── Build processors (shared by GPU and CPU paths) ────────────────────────
    // Override timing with live UI values (preset provides defaults)
    MasterFilm::TimingParams timing = preset->timing;
    timing.printerLightR = static_cast<float>(printerLightR);
    timing.printerLightG = static_cast<float>(printerLightG);
    timing.printerLightB = static_cast<float>(printerLightB);

    MasterFilm::PrintParams print = preset->print;
    print.usePrintCurve = usePrintCurve;

    MasterFilm::UnifiedFilmProcessor filmProc(preset->tone, timing,
                                              print, preset->color);

    MasterFilm::HalationProcessor halationProc(preset->halation);

    // Set the per-frame seed from render time into the stock's grain profile.
    MasterFilm::FilmStockProfile grainProfile = preset->grainProfile;
    grainProfile.frame_index = static_cast<int32_t>(renderTime * 24.0);
    const int renderSeed = grainProfile.frame_index;  // kept for CPU fallback API compat
    MasterFilm::GrainProcessor grainProc(grainProfile);

    MasterFilm::AcutanceProcessor acutanceProc(preset->acutance);

    // ── GPU-only path ──────────────────────────────────────────────────────────
#ifdef MASTERFILM_ENABLE_OPENGL
    {
        int glEnabled = 0;
        if (gGLSuite)
            gPropSuite->propGetInt(inArgs, kOfxImageEffectPropOpenGLEnabled, 0, &glEnabled);

        MasterFilm::GLSLDispatch* gl = nullptr;
        if (glEnabled) {
            std::lock_guard<std::mutex> lk(gGLDispatchersMutex);
            auto it = gGLDispatchers.find(instance);
            if (it != gGLDispatchers.end()) gl = it->second;
        }

        // Diagnostics — written to stderr so they show in Resolve's console log.
        if (!gGLSuite)
            std::fprintf(stderr, "[MasterFilm] GPU: gGLSuite is null — host didn't provide OpenGL render suite\n");
        else if (!glEnabled)
            std::fprintf(stderr, "[MasterFilm] GPU: glEnabled=0 — host did not enable OpenGL for this render\n");
        else if (!gl)
            std::fprintf(stderr, "[MasterFilm] GPU: GLSLDispatch not found — shader init may have failed\n");
        else if (!enableGPU)
            std::fprintf(stderr, "[MasterFilm] GPU: enableGPU checkbox is OFF\n");

        if (gl && enableGPU) {
            setRenderPathLabel("GPU (OpenGL)");
            // GL resource handles — declared before try so catch can clean them up.
            OfxPropertySetHandle srcTexHandle = nullptr;
            GLuint negLUTTex = 0, printLUTTex = 0;
            GLuint texA = 0, texB = 0, texC = 0;
            GLuint fboA = 0, fboB = 0, fboC = 0;

            try {
                // ── Load source as GL texture via OFX OpenGL render suite ────
                OfxStatus texStat = gGLSuite->clipLoadTexture(srcClip, renderTime, nullptr, nullptr, &srcTexHandle);
                if (texStat != kOfxStatOK || !srcTexHandle)
                    throw std::runtime_error("GPU: clipLoadTexture(src) failed");

                int srcTexIndex = 0;
                gPropSuite->propGetInt(srcTexHandle, kOfxImageEffectPropOpenGLTextureIndex, 0, &srcTexIndex);
                GLuint srcTex = static_cast<GLuint>(srcTexIndex);

                // Check texture target — Resolve may give GL_TEXTURE_RECTANGLE
                int srcTexTarget = GL_TEXTURE_2D;
                gPropSuite->propGetInt(srcTexHandle, kOfxImageEffectPropOpenGLTextureTarget, 0, &srcTexTarget);
                // For now we require GL_TEXTURE_2D (most common in Resolve GPU mode).
                // If the host returns GL_TEXTURE_RECTANGLE, fall through to error.
                if (srcTexTarget != GL_TEXTURE_2D)
                    throw std::runtime_error("GPU: unsupported texture target (expected GL_TEXTURE_2D)");

                // ── Query host output FBO ────────────────────────────────────
                GLint hostFBO = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &hostFBO);

                // ── Build and upload LUT textures ────────────────────────────
                filmProc.ensurePrintLUT(colorSpaceMode);

                negLUTTex = MasterFilm::GLSLDispatch::uploadLUTRow(
                    filmProc.negLUT_R().data(),
                    filmProc.negLUT_G().data(),
                    filmProc.negLUT_B().data(),
                    MasterFilm::UnifiedFilmProcessor::gpuLUTSize());

                printLUTTex = MasterFilm::GLSLDispatch::uploadLUTRow(
                    filmProc.printLUT_R().data(),
                    filmProc.printLUT_G().data(),
                    filmProc.printLUT_B().data(),
                    MasterFilm::UnifiedFilmProcessor::gpuPrintLUTSize());

                // ── Create three intermediate texture/FBO pairs ──────────────
                // texC holds tone/color result (preserved for halation_v uSrc).
                // texA/texB are the ping-pong pair for subsequent passes.
                createFloatTexAndFBO(width, height, texA, fboA);
                createFloatTexAndFBO(width, height, texB, fboB);
                createFloatTexAndFBO(width, height, texC, fboC);

                // ── Precompute GPU uniforms ──────────────────────────────────
                float exitDMidR, exitDMidG, exitDMidB;
                filmProc.getExitDMid(exitDMidR, exitDMidG, exitDMidB);

                float lumaWR, lumaWG, lumaWB;
                MasterFilm::UnifiedFilmProcessor::gpuGetLumaCoeffs(colorSpaceMode, lumaWR, lumaWG, lumaWB);

                const auto& cm = filmProc.couplingMatrix();

                // ── Pass 0 — Tone + Color  (srcTex → fboC; result in texC) ──
                {
                    MasterFilm::ShaderProgram& prog = gl->toneColorShader();
                    glUseProgram(prog.id);

                    // Bind LUT textures to units 2 and 3
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, negLUTTex);
                    glUniform1i(prog.loc("uNegLUT"), 2);

                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, printLUTTex);
                    glUniform1i(prog.loc("uPrintLUT"), 3);
                    glActiveTexture(GL_TEXTURE0);

                    // Color space
                    glUniform1i(prog.loc("uColorSpaceMode"), colorSpaceChoice);

                    // Neg LUT domain
                    glUniform1f(prog.loc("uStopsMin"),   MasterFilm::UnifiedFilmProcessor::gpuStopsMin());
                    glUniform1f(prog.loc("uStopsRange"), MasterFilm::UnifiedFilmProcessor::gpuStopsRange());
                    glUniform1f(prog.loc("uLUTSize"),    static_cast<float>(MasterFilm::UnifiedFilmProcessor::gpuLUTSize()));

                    // Print LUT domain
                    glUniform1f(prog.loc("uDeltaMin"),      MasterFilm::UnifiedFilmProcessor::gpuDeltaMin());
                    glUniform1f(prog.loc("uDeltaRange"),    MasterFilm::UnifiedFilmProcessor::gpuDeltaRange());
                    glUniform1f(prog.loc("uPrintLUTSize"),  static_cast<float>(MasterFilm::UnifiedFilmProcessor::gpuPrintLUTSize()));

                    // Film parameters
                    glUniform1f(prog.loc("uFilmColor"),  filmProc.filmColor());
                    glUniform3f(prog.loc("uExitDMid"),   exitDMidR, exitDMidG, exitDMidB);
                    glUniform3f(prog.loc("uTimingOffset"),
                                filmProc.timingOffR(), filmProc.timingOffG(), filmProc.timingOffB());
                    glUniform1f(prog.loc("uLog2of10"),   MasterFilm::UnifiedFilmProcessor::gpuLog2of10());

                    // Coupling matrix (row-major float[9] → GLSL mat3 column-major with GL_TRUE transpose)
                    glUniformMatrix3fv(prog.loc("uCouplingMatrix"), 1, GL_TRUE, cm.data());
                    glUniform1i(prog.loc("uCouplingIsIdentity"), filmProc.couplingIsIdentity() ? 1 : 0);

                    // Enable flags
                    glUniform1i(prog.loc("uEnableTone"),  enableTone  ? 1 : 0);
                    glUniform1i(prog.loc("uEnableColor"), enableColor ? 1 : 0);

                    // Zone color parameters
                    const auto& cp = filmProc.colorParams();
                    glUniform1f(prog.loc("uHueShadow"),    cp.hueShadowShift);
                    glUniform1f(prog.loc("uHueMid"),       cp.hueMidShift);
                    glUniform1f(prog.loc("uHueHighlight"), cp.hueHighlightShift);
                    glUniform1f(prog.loc("uSatShadow"),    cp.satShadow);
                    glUniform1f(prog.loc("uSatMid"),       cp.satMid);
                    glUniform1f(prog.loc("uSatHighlight"), cp.satHighlight);
                    glUniform3f(prog.loc("uLumaCoeffs"),   lumaWR, lumaWG, lumaWB);

                    glUseProgram(0);
                    gl->renderPass(prog, srcTex, fboC, width, height);
                }

                // ── Pass 1 — Halation Horizontal  (texC → fboA; result in texA) ──
                {
                    MasterFilm::ShaderProgram& prog = gl->halationHShader();
                    const float innerRadius = preset->halation.radius * static_cast<float>(height) * 0.03f;
                    glUseProgram(prog.id);
                    glUniform1f(prog.loc("uInnerRadius"),      innerRadius);
                    glUniform1f(prog.loc("uOuterRadiusScale"), preset->halation.outerRadiusScale);
                    glUniform1f(prog.loc("uOuterWeight"),      preset->halation.outerWeight);
                    glUniform1f(prog.loc("uThreshold"),        preset->halation.threshold);
                    glUniform3f(prog.loc("uSpecBias"),
                                preset->halation.biasR, preset->halation.biasG, preset->halation.biasB);
                    glUseProgram(0);
                    gl->renderPass(prog, texC, fboA, width, height);
                }

                // ── Pass 2 — Halation Vertical + Composite ──
                //   uSrc  = texC (tone/color result, pre-halation)
                //   uHBlur = texA (horizontal-blur from Pass 1)
                //   result → fboB; composite in texB
                //   uIntensity = 0 when halation is disabled → shader outputs src unchanged.
                {
                    MasterFilm::ShaderProgram& prog = gl->halationVShader();
                    const float innerRadius = preset->halation.radius * static_cast<float>(height) * 0.03f;
                    glUseProgram(prog.id);
                    glUniform1f(prog.loc("uInnerRadius"),      innerRadius);
                    glUniform1f(prog.loc("uOuterRadiusScale"), preset->halation.outerRadiusScale);
                    glUniform1f(prog.loc("uOuterWeight"),      preset->halation.outerWeight);
                    glUniform1f(prog.loc("uThreshold"),        preset->halation.threshold);
                    glUniform3f(prog.loc("uSpecBias"),
                                preset->halation.biasR, preset->halation.biasG, preset->halation.biasB);
                    glUniform1f(prog.loc("uIntensity"),
                                enableHalation ? preset->halation.intensity : 0.0f);
                    glUseProgram(0);
                    gl->renderPass(prog, texC, fboB, width, height, texA, "uHBlur");
                }

                // ── Pass 3 — Grain  (texB → fboA; result in texA) ──
                //   uRMSGranularity = 0 when grain is disabled → shader outputs src unchanged.
                {
                    MasterFilm::ShaderProgram& prog = gl->grainShader();
                    const MasterFilm::FilmStockProfile& gp = grainProfile;

                    glUseProgram(prog.id);
                    glUniform1f(prog.loc("uRMSGranularity"),
                                enableGrain ? gp.rms_granularity : 0.0f);
                    glUniform1i(prog.loc("uMorphologyType"),      gp.morphology_type);
                    glUniform4fv(prog.loc("uARCoefficients"),  1, gp.ar_coefficients);
                    glUniform1f(prog.loc("uARSigma"),             gp.ar_sigma);
                    glUniformMatrix3fv(prog.loc("uSpectralMatrix"), 1, GL_TRUE,
                                       gp.spectral_matrix);
                    glUniform1f(prog.loc("uChromaMicroContrast"),  gp.chroma_micro_contrast);
                    glUniform1fv(prog.loc("uTonalLUT"),
                                 MasterFilm::kTonalLUTSize, gp.tonal_lut);
                    glUniform1ui(prog.loc("uGlobalSeed"),         gp.global_seed);
                    glUniform1i(prog.loc("uFrameIndex"),          gp.frame_index);
                    glUniform1f(prog.loc("uGrainSize"),           gp.grain_size);
                    glUniform1f(prog.loc("uISO"),                 gp.iso);
                    glUseProgram(0);
                    gl->renderPass(prog, texB, fboA, width, height);
                }

                // ── Pass 4 — Acutance  (texA → host output FBO) ──
                //   uIntensity = 0 when acutance is disabled → shader outputs src unchanged.
                {
                    MasterFilm::ShaderProgram& prog = gl->acutanceShader();
                    glUseProgram(prog.id);
                    glUniform1i(prog.loc("uCharacter"),        static_cast<int>(preset->acutance.character));
                    glUniform1f(prog.loc("uIntensity"),
                                enableAcutance ? preset->acutance.intensity : 0.0f);
                    glUniform1f(prog.loc("uRolloff"),           preset->acutance.rolloff);
                    glUniform1f(prog.loc("uKostinskyStrength"), preset->acutance.kostinskyStrength);
                    glUseProgram(0);
                    gl->renderPass(prog, texA, static_cast<GLuint>(hostFBO), width, height);
                }

                // ── Cleanup GL resources ─────────────────────────────────────
                glDeleteTextures(1, &negLUTTex);   negLUTTex  = 0;
                glDeleteTextures(1, &printLUTTex);  printLUTTex = 0;
                glDeleteTextures(1, &texA);         texA = 0;
                glDeleteTextures(1, &texB);         texB = 0;
                glDeleteTextures(1, &texC);         texC = 0;
                glDeleteFramebuffers(1, &fboA);     fboA = 0;
                glDeleteFramebuffers(1, &fboB);     fboB = 0;
                glDeleteFramebuffers(1, &fboC);     fboC = 0;
                gGLSuite->clipFreeTexture(srcTexHandle); srcTexHandle = nullptr;

                return kOfxStatOK;
            }
            catch (const std::exception& ex) {
                std::fprintf(stderr, "[MasterFilm] GPU exception: %s\n", ex.what());
                // Clean up any GL resources that were created before the exception.
                if (negLUTTex)  glDeleteTextures(1, &negLUTTex);
                if (printLUTTex) glDeleteTextures(1, &printLUTTex);
                if (texA)   glDeleteTextures(1, &texA);
                if (texB)   glDeleteTextures(1, &texB);
                if (texC)   glDeleteTextures(1, &texC);
                if (fboA)   glDeleteFramebuffers(1, &fboA);
                if (fboB)   glDeleteFramebuffers(1, &fboB);
                if (fboC)   glDeleteFramebuffers(1, &fboC);
                if (srcTexHandle) gGLSuite->clipFreeTexture(srcTexHandle);

                // Black-fill the destination so GPU failures are visually obvious during testing.
                OfxPropertySetHandle dstImgBlack = nullptr;
                gEffectSuite->clipGetImage(dstClip, renderTime, nullptr, &dstImgBlack);
                if (dstImgBlack) {
                    void* dstPtrBlack = nullptr;
                    gPropSuite->propGetPointer(dstImgBlack, kOfxImagePropData, 0, &dstPtrBlack);
                    int dstRowBytesBlack = 0;
                    gPropSuite->propGetInt(dstImgBlack, kOfxImagePropRowBytes, 0, &dstRowBytesBlack);
                    OfxRectI dstBoundsBlack{};
                    gPropSuite->propGetInt(dstImgBlack, kOfxImagePropBounds, 0, &dstBoundsBlack.x1);
                    gPropSuite->propGetInt(dstImgBlack, kOfxImagePropBounds, 1, &dstBoundsBlack.y1);
                    gPropSuite->propGetInt(dstImgBlack, kOfxImagePropBounds, 2, &dstBoundsBlack.x2);
                    gPropSuite->propGetInt(dstImgBlack, kOfxImagePropBounds, 3, &dstBoundsBlack.y2);
                    if (dstPtrBlack) {
                        for (int y = 0; y < height; ++y) {
                            float* row = reinterpret_cast<float*>(
                                static_cast<char*>(dstPtrBlack)
                                + static_cast<ptrdiff_t>(renderWindow.y1 + y - dstBoundsBlack.y1) * dstRowBytesBlack
                                + static_cast<ptrdiff_t>(renderWindow.x1 - dstBoundsBlack.x1) * kBytesPerPixel);
                            std::memset(row, 0, static_cast<size_t>(width * kBytesPerPixel));
                        }
                    }
                    gEffectSuite->clipReleaseImage(dstImgBlack);
                }
                return kOfxStatOK;
            }
        }
    }
#endif // MASTERFILM_ENABLE_OPENGL

    // ── CPU fallback — runs when host doesn't provide a GL context ─────────
    // This path is slower (per-pixel CPU loops + heap alloc) but ensures the
    // plugin works regardless of the host's GPU configuration.
    setRenderPathLabel("CPU (fallback)");
    std::fprintf(stderr, "[MasterFilm] Falling back to CPU render path\n");

    OfxPropertySetHandle srcImg = nullptr;
    OfxPropertySetHandle dstImg = nullptr;
    gEffectSuite->clipGetImage(srcClip, renderTime, nullptr, &srcImg);
    gEffectSuite->clipGetImage(dstClip, renderTime, nullptr, &dstImg);

    if (!srcImg || !dstImg) {
        if (srcImg) gEffectSuite->clipReleaseImage(srcImg);
        if (dstImg) gEffectSuite->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    void* srcPtr = nullptr;
    void* dstPtr = nullptr;
    gPropSuite->propGetPointer(srcImg, kOfxImagePropData, 0, &srcPtr);
    gPropSuite->propGetPointer(dstImg, kOfxImagePropData, 0, &dstPtr);

    int srcRowBytes = 0, dstRowBytes = 0;
    gPropSuite->propGetInt(srcImg, kOfxImagePropRowBytes, 0, &srcRowBytes);
    gPropSuite->propGetInt(dstImg, kOfxImagePropRowBytes, 0, &dstRowBytes);

    OfxRectI srcBounds{}, dstBounds{};
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

    std::vector<float> bufA(static_cast<size_t>(nPixels * kNComp));
    std::vector<float> bufB(static_cast<size_t>(nPixels * kNComp));

    for (int y = 0; y < height; ++y) {
        const float* srcRow = reinterpret_cast<const float*>(
            static_cast<const char*>(srcPtr)
            + static_cast<ptrdiff_t>(renderWindow.y1 + y - srcBounds.y1) * srcRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - srcBounds.x1) * kBytesPerPixel);
        std::memcpy(&bufA[static_cast<size_t>(y * width * kNComp)], srcRow,
                    static_cast<size_t>(width * kBytesPerPixel));
    }

    const size_t cpuBufBytes = static_cast<size_t>(nPixels * kNComp) * sizeof(float);

    // Film (Tone+Color unified)
    if (enableTone || enableColor)
        filmProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp, colorSpaceMode);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);
    std::swap(bufA, bufB);

    // Halation
    if (enableHalation)
        halationProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);

    // Grain
    if (enableGrain)
        grainProc.processCPU(bufB.data(), bufA.data(), width, height, kNComp, renderSeed);
    else
        std::memcpy(bufA.data(), bufB.data(), cpuBufBytes);

    // Acutance
    if (enableAcutance)
        acutanceProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);

    for (int y = 0; y < height; ++y) {
        float* dstRow = reinterpret_cast<float*>(
            static_cast<char*>(dstPtr)
            + static_cast<ptrdiff_t>(renderWindow.y1 + y - dstBounds.y1) * dstRowBytes
            + static_cast<ptrdiff_t>(renderWindow.x1 - dstBounds.x1) * kBytesPerPixel);
        std::memcpy(dstRow, &bufB[static_cast<size_t>(y * width * kNComp)],
                    static_cast<size_t>(width * kBytesPerPixel));
    }

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

#ifdef MASTERFILM_ENABLE_OPENGL
        if (std::strcmp(action, kOfxActionOpenGLContextAttached) == 0) {
            auto instance = reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
            auto* gl = new MasterFilm::GLSLDispatch();
            std::string resourceDir = gBundlePath + "/Contents/Resources";
            if (!gl->initShaders(resourceDir)) {
                delete gl;
                return kOfxStatFailed;
            }
            std::lock_guard<std::mutex> lk(gGLDispatchersMutex);
            gGLDispatchers[instance] = gl;
            return kOfxStatOK;
        }
        if (std::strcmp(action, kOfxActionOpenGLContextDetached) == 0) {
            auto instance = reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
            std::lock_guard<std::mutex> lk(gGLDispatchersMutex);
            auto it = gGLDispatchers.find(instance);
            if (it != gGLDispatchers.end()) {
                delete it->second;
                gGLDispatchers.erase(it);
            }
            return kOfxStatOK;
        }
#endif

        return kOfxStatReplyDefault;
    }
    catch (const std::exception& e) {
        (void)e;
        return kOfxStatFailed;
    }
}
