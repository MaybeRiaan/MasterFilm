// src/platform/MetalDispatch.mm
// Objective-C++ bridge for Metal GPU compute dispatch.
// All Metal API calls are isolated here so the rest of the codebase stays pure C++.

#ifdef MASTERFILM_ENABLE_METAL

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "MetalDispatch.h"
#include <unordered_map>
#include <string>
#include <cstdio>

namespace MasterFilm {

// ── Opaque implementation ─────────────────────────────────────────────────────

struct MetalContextImpl {
    id<MTLDevice>              device     = nil;
    id<MTLCommandQueue>        queue      = nil;
    id<MTLLibrary>             library    = nil;
    std::unordered_map<std::string, id<MTLComputePipelineState>> pipelines;
};

// ── MetalDispatch ─────────────────────────────────────────────────────────────

MetalDispatch::MetalDispatch()
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            std::fprintf(stderr, "[MasterFilm] Metal: no default device\n");
            return;
        }
        mImpl = new MetalContextImpl();
        mImpl->device = dev;
        mImpl->queue  = [dev newCommandQueue];
    }
}

MetalDispatch::~MetalDispatch()
{
    delete mImpl;
}

bool MetalDispatch::initPipelines(const std::string& resourceDir)
{
    if (!mImpl) return false;

    @autoreleasepool {
        NSString* metalPath = [NSString stringWithFormat:@"%s/shaders/masterfilm.metallib",
                               resourceDir.c_str()];

        NSURL* url = [NSURL fileURLWithPath:metalPath];
        NSError* err = nil;
        mImpl->library = [mImpl->device newLibraryWithURL:url error:&err];

        if (!mImpl->library) {
            // Fallback: try to compile .metal source at runtime (development only)
            std::fprintf(stderr,
                "[MasterFilm] Metal: could not load metallib (%s). "
                "Did you compile shaders/metal/*.metal → masterfilm.metallib?\n",
                [[err localizedDescription] UTF8String]);
            return false;
        }

        // Register each named compute function as a pipeline
        NSArray<NSString*>* functions = @[
            @"grain_main",
            @"halation_h_main",
            @"halation_v_main",
            @"tone_color_main",
            @"acutance_main"
        ];

        for (NSString* fnName in functions) {
            id<MTLFunction> fn = [mImpl->library newFunctionWithName:fnName];
            if (!fn) {
                std::fprintf(stderr, "[MasterFilm] Metal: missing function %s\n",
                             [fnName UTF8String]);
                continue;
            }
            NSError* pipeErr = nil;
            id<MTLComputePipelineState> pso =
                [mImpl->device newComputePipelineStateWithFunction:fn error:&pipeErr];
            if (!pso) {
                std::fprintf(stderr, "[MasterFilm] Metal: pipeline error for %s: %s\n",
                             [fnName UTF8String],
                             [[pipeErr localizedDescription] UTF8String]);
                continue;
            }
            mImpl->pipelines[[fnName UTF8String]] = pso;
        }

        return !mImpl->pipelines.empty();
    }
}

bool MetalDispatch::dispatch(const std::string& pipelineName,
                             void* srcTexHandle,
                             void* dstTexHandle,
                             int width, int height)
{
    if (!mImpl) return false;

    auto it = mImpl->pipelines.find(pipelineName);
    if (it == mImpl->pipelines.end()) {
        std::fprintf(stderr, "[MasterFilm] Metal: unknown pipeline %s\n",
                     pipelineName.c_str());
        return false;
    }

    @autoreleasepool {
        id<MTLTexture> srcTex = (__bridge id<MTLTexture>)srcTexHandle;
        id<MTLTexture> dstTex = (__bridge id<MTLTexture>)dstTexHandle;

        id<MTLCommandBuffer>        cmd     = [mImpl->queue commandBuffer];
        id<MTLComputeCommandEncoder> enc    = [cmd computeCommandEncoder];

        [enc setComputePipelineState:it->second];
        [enc setTexture:srcTex atIndex:0];
        [enc setTexture:dstTex atIndex:1];

        // TODO: set per-pass uniform buffers before dispatch

        NSUInteger tgSize  = it->second.maxTotalThreadsPerThreadgroup;
        NSUInteger tgWidth = (tgSize > 32) ? 32 : tgSize;
        MTLSize threadgroupSize = MTLSizeMake(tgWidth, tgWidth, 1);
        MTLSize gridSize = MTLSizeMake(
            (width  + tgWidth - 1) / tgWidth,
            (height + tgWidth - 1) / tgWidth,
            1
        );

        [enc dispatchThreadgroups:gridSize threadsPerThreadgroup:threadgroupSize];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
    }

    return true;
}

} // namespace MasterFilm

#endif // MASTERFILM_ENABLE_METAL
