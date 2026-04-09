// src/processors/AcutanceProcessor.cpp
// Pass 5: edge emphasis / sharpness character.
// Luminance-based unsharp mask with optional Kostinsky boost.
#include "AcutanceProcessor.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace MasterFilm {

OfxStatus AcutanceProcessor::processGPU(OfxImageEffectHandle, OfxPropertySetHandle, OfxPropertySetHandle) const {
    // TODO: unsharp-mask (Soft/Natural) + Laplacian+Kostinsky term (Enhanced)
    // Shader: shaders/glsl/acutance.glsl
    return kOfxStatReplyDefault;
}

OfxStatus AcutanceProcessor::processCPU(const float* src, float* dst,
                                         int width, int height, int nComponents) const {
    if (!src || !dst || width <= 0 || height <= 0 || nComponents < 3)
        return kOfxStatFailed;

    // --- Character mode parameters ---
    float blurRadius, strength;
    switch (mParams.character) {
        case AcutanceCharacter::Soft:
            blurRadius = 2.0f;
            strength   = 0.6f;
            break;
        case AcutanceCharacter::Natural:
            blurRadius = 3.0f;
            strength   = 0.8f;
            break;
        case AcutanceCharacter::Enhanced:
        default:
            blurRadius = 4.0f;
            strength   = 1.0f;
            break;
    }

    const int numPixels = width * height;

    // --- Allocate buffers ---
    std::vector<float> lumaMap(numPixels);
    std::vector<float> blurredLuma(numPixels);
    std::vector<float> tempBlur(numPixels);

    // --- Step 1: Build luminance map ---
    for (int i = 0; i < numPixels; ++i) {
        const float* px = src + i * nComponents;
        lumaMap[i] = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
    }

    // --- Step 2: Build 1D Gaussian kernel ---
    const int halfWidth = std::min(static_cast<int>(std::ceil(3.0f * blurRadius)), 64);
    const int kernelSize = 2 * halfWidth + 1;
    std::vector<float> kernel(kernelSize);
    float kernelSum = 0.0f;
    for (int k = -halfWidth; k <= halfWidth; ++k) {
        float val = std::exp(-0.5f * (k * k) / (blurRadius * blurRadius));
        kernel[k + halfWidth] = val;
        kernelSum += val;
    }
    // Normalize
    for (int k = 0; k < kernelSize; ++k)
        kernel[k] /= kernelSum;

    // --- Step 2a: Horizontal pass (lumaMap -> tempBlur) ---
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -halfWidth; k <= halfWidth; ++k) {
                int sx = std::max(0, std::min(width - 1, x + k));
                acc += lumaMap[y * width + sx] * kernel[k + halfWidth];
            }
            tempBlur[y * width + x] = acc;
        }
    }

    // --- Step 2b: Vertical pass (tempBlur -> blurredLuma) ---
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -halfWidth; k <= halfWidth; ++k) {
                int sy = std::max(0, std::min(height - 1, y + k));
                acc += tempBlur[sy * width + x] * kernel[k + halfWidth];
            }
            blurredLuma[y * width + x] = acc;
        }
    }

    // --- Step 3: Apply sharpening per pixel ---
    const bool isEnhanced = (mParams.character == AcutanceCharacter::Enhanced);
    const float intensity  = mParams.intensity;
    const float rolloff    = mParams.rolloff;
    const float kostinsky  = mParams.kostinskyStrength;

    for (int i = 0; i < numPixels; ++i) {
        const float luma    = lumaMap[i];
        const float blurred = blurredLuma[i];
        const float detail  = luma - blurred;

        // a. Kostinsky boost for Enhanced mode only
        float detail_signal;
        if (isEnhanced) {
            detail_signal = detail * (1.0f + kostinsky * std::abs(detail) * 10.0f);
        } else {
            detail_signal = detail;
        }

        // b. Rolloff factor: midtone-centric when rolloff > 0
        //    rolloff=0 -> uniform; rolloff=1 -> strongest at midtones (luma=0.5)
        float rolloff_factor = 1.0f - rolloff * (1.0f - 2.0f * std::abs(luma - 0.5f));
        rolloff_factor = std::max(0.0f, std::min(1.0f, rolloff_factor));

        // c. Combined sharpening delta (applied equally to all RGB channels)
        const float sharpen = detail_signal * intensity * rolloff_factor * strength;

        // d. Write output channels
        const float* srcPx = src + i * nComponents;
        float*       dstPx = dst + i * nComponents;

        dstPx[0] = srcPx[0] + sharpen;
        dstPx[1] = srcPx[1] + sharpen;
        dstPx[2] = srcPx[2] + sharpen;

        // e. Alpha passthrough
        if (nComponents == 4)
            dstPx[3] = srcPx[3];
    }

    return kOfxStatOK;
}

} // namespace MasterFilm
