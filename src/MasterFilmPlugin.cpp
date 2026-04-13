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
    gPropSuite->propSetString(effectProps, kOfxImageEffectPropOpenGLRenderSupported, 0, "true");
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

    const int renderSeed = static_cast<int>(renderTime * 24.0);
    MasterFilm::GrainProcessor grainProc(preset->grain);

    MasterFilm::AcutanceProcessor acutanceProc(preset->acutance);

    // ── GPU path ──────────────────────────────────────────────────────────────
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

        if (gl && enableGPU) {
            // Declare all GL resource handles before the try so the catch can clean them up.
            OfxPropertySetHandle gpuSrcImg = nullptr;
            GLuint cpuTex = 0, texA = 0, texB = 0, fboA = 0, fboB = 0;

            try {
                // Fetch source pixels for CPU Tone+Color pre-pass.
                gEffectSuite->clipGetImage(srcClip, renderTime, nullptr, &gpuSrcImg);
                if (!gpuSrcImg) throw std::runtime_error("GPU: clipGetImage(src) failed");

                void* srcPtr = nullptr;
                gPropSuite->propGetPointer(gpuSrcImg, kOfxImagePropData, 0, &srcPtr);
                int srcRowBytes = 0;
                gPropSuite->propGetInt(gpuSrcImg, kOfxImagePropRowBytes, 0, &srcRowBytes);
                OfxRectI srcBounds{};
                gPropSuite->propGetInt(gpuSrcImg, kOfxImagePropBounds, 0, &srcBounds.x1);
                gPropSuite->propGetInt(gpuSrcImg, kOfxImagePropBounds, 1, &srcBounds.y1);
                gPropSuite->propGetInt(gpuSrcImg, kOfxImagePropBounds, 2, &srcBounds.x2);
                gPropSuite->propGetInt(gpuSrcImg, kOfxImagePropBounds, 3, &srcBounds.y2);
                if (!srcPtr) throw std::runtime_error("GPU: null srcPtr");

                // Destride source into bufA.
                std::vector<float> bufA_cpu(static_cast<size_t>(nPixels * kNComp));
                std::vector<float> bufB_cpu(static_cast<size_t>(nPixels * kNComp));
                for (int y = 0; y < height; ++y) {
                    const float* srcRow = reinterpret_cast<const float*>(
                        static_cast<const char*>(srcPtr)
                        + static_cast<ptrdiff_t>(renderWindow.y1 + y - srcBounds.y1) * srcRowBytes
                        + static_cast<ptrdiff_t>(renderWindow.x1 - srcBounds.x1) * kBytesPerPixel);
                    std::memcpy(&bufA_cpu[static_cast<size_t>(y * width * kNComp)], srcRow,
                                static_cast<size_t>(width * kBytesPerPixel));
                }

                // CPU pre-pass: Unified film processor (Tone+Color combined).
                // When both Tone and Color are disabled, passthrough.
                // The unified processor handles all four stages internally.
                const size_t bufBytes = static_cast<size_t>(nPixels * kNComp) * sizeof(float);
                if (enableTone || enableColor)
                    filmProc.processCPU(bufA_cpu.data(), bufB_cpu.data(), width, height, kNComp, colorSpaceMode);
                else
                    std::memcpy(bufB_cpu.data(), bufA_cpu.data(), bufBytes);

                // Swap to bufA for consistency with downstream GPU passes.
                std::swap(bufA_cpu, bufB_cpu);
                // bufA_cpu now holds the unified film result (or passthrough).

                // Query host output FBO before any renderPass() call (renderPass rebinds to 0).
                GLint hostFBO = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &hostFBO);

                // Upload CPU result to a GL texture.
                glGenTextures(1, &cpuTex);
                glBindTexture(GL_TEXTURE_2D, cpuTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0,
                             GL_RGBA, GL_FLOAT, bufA_cpu.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D, 0);

                // Create two intermediate ping-pong texture/FBO pairs.
                createFloatTexAndFBO(width, height, texA, fboA);
                createFloatTexAndFBO(width, height, texB, fboB);

                // Pass 1 — Halation Horizontal  (cpuTex → fboA; result in texA)
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
                    gl->renderPass(prog, cpuTex, fboA, width, height);
                }

                // Pass 2 — Halation Vertical + Composite
                //   uSrc  = cpuTex (original pre-halation image)
                //   uHBlur = texA  (horizontal-blur output from Pass 1)
                //   result → fboB; composite in texB
                //   uIntensity = 0 when halation is disabled → shader outputs src unchanged.
                {
                    MasterFilm::ShaderProgram& prog = gl->halationVShader();
                    const float innerRadius = preset->halation.radius * static_cast<float>(height) * 0.03f;
                    glUseProgram(prog.id);
                    glUniform1f(prog.loc("uInnerRadius"),      innerRadius);
                    glUniform1f(prog.loc("uOuterRadiusScale"), preset->halation.outerRadiusScale);
                    glUniform1f(prog.loc("uOuterWeight"),      preset->halation.outerWeight);
                    glUniform1f(prog.loc("uThreshold"),        preset->halation.threshold);   // declared but unused in shader, safe
                    glUniform3f(prog.loc("uSpecBias"),                                         // declared but unused in shader, safe
                                preset->halation.biasR, preset->halation.biasG, preset->halation.biasB);
                    glUniform1f(prog.loc("uIntensity"),
                                enableHalation ? preset->halation.intensity : 0.0f);
                    glUseProgram(0);
                    gl->renderPass(prog, cpuTex, fboB, width, height, texA, "uHBlur");
                }

                // Pass 3 — Grain  (texB → fboA; result overwrites texA)
                //   uAmount = 0 when grain is disabled → shader outputs src unchanged.
                {
                    MasterFilm::ShaderProgram& prog = gl->grainShader();
                    // Inline GrainProcessor::sizeToSigma (it is private): k * sqrt(iso/100) * (0.3 + size*2.7)
                    const float k = 0.35f;
                    const float sigma = k * std::sqrt(preset->grain.iso / 100.0f)
                                          * (0.3f + preset->grain.size * 2.7f);
                    glUseProgram(prog.id);
                    glUniform1f(prog.loc("uAmount"),    enableGrain ? preset->grain.amount : 0.0f);
                    glUniform1f(prog.loc("uSigma"),     sigma);
                    glUniform1f(prog.loc("uRoughness"), preset->grain.roughness);
                    glUniform3f(prog.loc("uZoneWeights"),
                                preset->grain.shadowWeight, preset->grain.midWeight, preset->grain.highlightWeight);
                    glUniform1i(prog.loc("uSeed"),      renderSeed);
                    glUseProgram(0);
                    gl->renderPass(prog, texB, fboA, width, height);   // result now in texA
                }

                // Pass 4 — Acutance  (texA → host output FBO)
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

                // Cleanup GL resources and source image.
                glDeleteTextures(1, &cpuTex);  cpuTex = 0;
                glDeleteTextures(1, &texA);    texA   = 0;
                glDeleteTextures(1, &texB);    texB   = 0;
                glDeleteFramebuffers(1, &fboA); fboA  = 0;
                glDeleteFramebuffers(1, &fboB); fboB  = 0;
                gEffectSuite->clipReleaseImage(gpuSrcImg); gpuSrcImg = nullptr;

                return kOfxStatOK;
            }
            catch (...) {
                // Clean up any GL resources that were created before the exception.
                if (cpuTex) glDeleteTextures(1, &cpuTex);
                if (texA)   glDeleteTextures(1, &texA);
                if (texB)   glDeleteTextures(1, &texB);
                if (fboA)   glDeleteFramebuffers(1, &fboA);
                if (fboB)   glDeleteFramebuffers(1, &fboB);
                if (gpuSrcImg) gEffectSuite->clipReleaseImage(gpuSrcImg);

                // Black-fill the destination so GPU failures are visually obvious during testing.
                // Correct output = GPU path worked.  Black frame = GPU path failed (check stderr).
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
                return kOfxStatOK;  // host sees "success"; black frame signals the failure visually
            }
        }
    }
#endif // MASTERFILM_ENABLE_OPENGL

    // ── CPU path (full pipeline) ──────────────────────────────────────────────
    // Runs when OpenGL is unavailable or not enabled by the host.

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

    // ── Render pipeline: Film → Halation → Grain → Acutance ────────────────────
    // The unified film processor replaces the old Tone + Color split.
    // Each stage reads from one buffer and writes to the other (ping-pong).
    // When a stage is disabled, memcpy preserves the alternating invariant so
    // the final result always lands in bufB regardless of which stages ran.
    const size_t cpuBufBytes = static_cast<size_t>(nPixels * kNComp) * sizeof(float);

    // bufA → Film (Tone+Color unified) → bufB
    if (enableTone || enableColor)
        filmProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp, colorSpaceMode);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);

    // bufB → (skip one ping-pong step — was Color) → bufA
    // The unified processor consumed both Tone and Color in one pass,
    // so we swap to maintain the ping-pong invariant for downstream stages.
    std::swap(bufA, bufB);
    // bufA now has the film result

    // bufA → Halation → bufB
    if (enableHalation)
        halationProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);

    // bufB → Grain → bufA
    if (enableGrain)
        grainProc.processCPU(bufB.data(), bufA.data(), width, height, kNComp, renderSeed);
    else
        std::memcpy(bufA.data(), bufB.data(), cpuBufBytes);

    // bufA → Acutance → bufB
    if (enableAcutance)
        acutanceProc.processCPU(bufA.data(), bufB.data(), width, height, kNComp);
    else
        std::memcpy(bufB.data(), bufA.data(), cpuBufBytes);

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
