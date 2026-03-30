// src/MasterFilmPlugin.h
#pragma once

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxProperty.h"
#include "ofxParam.h"
#include "ofxMemory.h"
#include "ofxMultiThread.h"
#include "ofxMessage.h"
#include "ofxPixels.h"

// ── Global suite pointers (defined in MasterFilmPlugin.cpp) ──────────────────
extern OfxHost* gHost;
extern OfxPropertySuiteV1* gPropSuite;
extern OfxImageEffectSuiteV1* gEffectSuite;
extern OfxParameterSuiteV1* gParamSuite;
extern OfxMemorySuiteV1* gMemorySuite;
extern OfxMultiThreadSuiteV1* gThreadSuite;
extern OfxMessageSuiteV2* gMessageSuite;

// ── Version (injected by CMake) ───────────────────────────────────────────────
#ifndef MASTERFILM_VERSION_MAJOR
#  define MASTERFILM_VERSION_MAJOR 1
#endif
#ifndef MASTERFILM_VERSION_MINOR
#  define MASTERFILM_VERSION_MINOR 0
#endif
#ifndef MASTERFILM_VERSION_PATCH
#  define MASTERFILM_VERSION_PATCH 0
#endif

// ── Plugin mode ───────────────────────────────────────────────────────────────
enum class PluginMode { Pro, Lite };

// ── OFX export macro ──────────────────────────────────────────────────────────
// The real OFX SDK (ofxCore.h) already defines OfxExport on some platforms.
// Only define it ourselves if the SDK hasn't provided one.
#ifndef OfxExport
#  if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#    define OfxExport extern "C" __declspec(dllexport)
#  else
#    define OfxExport extern "C" __attribute__((visibility("default")))
#  endif
#endif