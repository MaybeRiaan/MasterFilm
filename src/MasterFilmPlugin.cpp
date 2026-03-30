// src/MasterFilmPlugin.cpp
// OFX entry points and plugin factory for MasterFilm.
// Registers both Pro and Lite plugin variants from a single binary.

#include "MasterFilmPlugin.h"
#include "ofxCore.h"
#include "ofxImageEffect.h"

#include <cstring>
#include <stdexcept>

// ── Forward declarations ──────────────────────────────────────────────────────
static OfxStatus pluginMain(const char* action,
    const void* handle,
    OfxPropertySetHandle inArgs,
    OfxPropertySetHandle outArgs);

// ── Global host suite pointers (populated at load time) ───────────────────────
OfxHost* gHost = nullptr;
OfxPropertySuiteV1* gPropSuite = nullptr;
OfxImageEffectSuiteV1* gEffectSuite = nullptr;
OfxParameterSuiteV1* gParamSuite = nullptr;
OfxMemorySuiteV1* gMemorySuite = nullptr;
OfxMultiThreadSuiteV1* gThreadSuite = nullptr;
OfxMessageSuiteV2* gMessageSuite = nullptr;

// ── Plugin descriptors ────────────────────────────────────────────────────────
// We expose two OFX plugins from one binary: Pro and Lite.
// The mode is encoded in the plugin identifier so Resolve lists them separately.

static OfxPlugin gPlugins[] = {
#ifdef MASTERFILM_BUILD_PRO
    {
        /* pluginApi */         kOfxImageEffectPluginApi,
        /* apiVersion */        1,
        /* pluginIdentifier */  "com.yourname.MasterFilmPro",
        /* pluginVersionMajor */ MASTERFILM_VERSION_MAJOR,
        /* pluginVersionMinor */ MASTERFILM_VERSION_MINOR,
        /* setHost */           nullptr,   // filled below
        /* mainEntry */         pluginMain
    },
#endif
#ifdef MASTERFILM_BUILD_LITE
    {
        /* pluginApi */         kOfxImageEffectPluginApi,
        /* apiVersion */        1,
        /* pluginIdentifier */  "com.yourname.MasterFilmLite",
        /* pluginVersionMajor */ MASTERFILM_VERSION_MAJOR,
        /* pluginVersionMinor */ MASTERFILM_VERSION_MINOR,
        /* setHost */           nullptr,
        /* mainEntry */         pluginMain
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

// ── Host fetch helper ─────────────────────────────────────────────────────────

static OfxStatus fetchSuites()
{
    if (!gHost) return kOfxStatErrMissingHostFeature;

    // fetchSuite returns const void* in the real SDK — cast away const via void*
    // This is the standard pattern used by all OFX host plugins
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

// ── Plugin main dispatch ──────────────────────────────────────────────────────

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
    // TODO: set effect properties (label, group, context, etc.)
    // Will delegate to ProUI or LiteUI based on plugin identifier
    (void)descriptor;
    return kOfxStatOK;
}

static OfxStatus onDescribeInContext(OfxImageEffectHandle descriptor,
    OfxPropertySetHandle inArgs)
{
    // TODO: define clips and parameters
    (void)descriptor;
    (void)inArgs;
    return kOfxStatOK;
}

static OfxStatus onCreateInstance(OfxImageEffectHandle instance)
{
    // TODO: allocate per-instance data
    (void)instance;
    return kOfxStatOK;
}

static OfxStatus onDestroyInstance(OfxImageEffectHandle instance)
{
    // TODO: free per-instance data
    (void)instance;
    return kOfxStatOK;
}

static OfxStatus onRender(OfxImageEffectHandle instance,
    OfxPropertySetHandle inArgs)
{
    // TODO: dispatch through the five-pass pipeline:
    //   1. ColorProcessor (tone + color matrix)
    //   2. HalationProcessor horizontal
    //   3. HalationProcessor vertical + composite
    //   4. GrainProcessor
    //   5. AcutanceProcessor
    (void)instance;
    (void)inArgs;
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

        // Unhandled actions are fine — return ReplyDefault
        return kOfxStatReplyDefault;
    }
    catch (const std::exception& e) {
        (void)e; // TODO: log via gMessageSuite if available
        return kOfxStatFailed;
    }
}