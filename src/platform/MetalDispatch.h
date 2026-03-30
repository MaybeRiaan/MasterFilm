// src/platform/MetalDispatch.h
// Metal GPU dispatch for Apple Silicon / macOS.
// Bridges C++ plugin code → Objective-C++ → Metal API.
// Compiled as .mm (Objective-C++) — included only when MASTERFILM_ENABLE_METAL is set.
#pragma once

#ifdef MASTERFILM_ENABLE_METAL

#include <string>

namespace MasterFilm {

// Opaque handle — Metal objects live behind this pointer, managed in the .mm file
struct MetalContextImpl;

class MetalDispatch {
public:
    MetalDispatch();
    ~MetalDispatch();

    // Build all five Metal compute pipelines from .metal source in resourceDir
    bool initPipelines(const std::string& resourceDir);

    // Dispatch a named pipeline on a texture pair
    // srcTexHandle / dstTexHandle are Metal texture IDs wrapped as void*
    bool dispatch(const std::string& pipelineName,
                  void* srcTexHandle,
                  void* dstTexHandle,
                  int width, int height);

    bool isAvailable() const { return mImpl != nullptr; }

private:
    MetalContextImpl* mImpl = nullptr;
};

} // namespace MasterFilm

#endif // MASTERFILM_ENABLE_METAL
