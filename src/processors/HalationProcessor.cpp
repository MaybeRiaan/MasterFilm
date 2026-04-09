// src/processors/HalationProcessor.cpp
#include "HalationProcessor.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MasterFilm {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static float smoothstep(float edge0, float edge1, float x)
{
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Build a normalized 1-D Gaussian kernel with half-width r (total 2r+1 taps).
// sigma is the standard deviation in pixels. Returns the half-radius used.
static std::vector<float> buildKernel(float sigma, int& outRadius)
{
    int r = static_cast<int>(std::ceil(3.0f * sigma));
    r = std::clamp(r, 1, 512);
    outRadius = r;

    std::vector<float> k(2 * r + 1);
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        float v = std::exp(-0.5f * (static_cast<float>(i) / sigma) *
                               (static_cast<float>(i) / sigma));
        k[i + r] = v;
        sum += v;
    }
    for (float& w : k)
        w /= sum;
    return k;
}

// Horizontal separable Gaussian pass.
// src is width*height*nComponents (RGB only — nComp == 3 assumed for haloSrc).
static void blurH(const float* src, float* dst,
                  int width, int height, int nComp,
                  const std::vector<float>& kernel, int r)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float acc[3] = {0.0f, 0.0f, 0.0f};
            for (int k = -r; k <= r; ++k) {
                int sx = std::clamp(x + k, 0, width - 1);
                const float* p = src + (y * width + sx) * nComp;
                float w = kernel[k + r];
                for (int c = 0; c < nComp; ++c)
                    acc[c] += p[c] * w;
            }
            float* q = dst + (y * width + x) * nComp;
            for (int c = 0; c < nComp; ++c)
                q[c] = acc[c];
        }
    }
}

// Vertical separable Gaussian pass.
static void blurV(const float* src, float* dst,
                  int width, int height, int nComp,
                  const std::vector<float>& kernel, int r)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float acc[3] = {0.0f, 0.0f, 0.0f};
            for (int k = -r; k <= r; ++k) {
                int sy = std::clamp(y + k, 0, height - 1);
                const float* p = src + (sy * width + x) * nComp;
                float w = kernel[k + r];
                for (int c = 0; c < nComp; ++c)
                    acc[c] += p[c] * w;
            }
            float* q = dst + (y * width + x) * nComp;
            for (int c = 0; c < nComp; ++c)
                q[c] = acc[c];
        }
    }
}

// ---------------------------------------------------------------------------
// radiusInPixels
// ---------------------------------------------------------------------------

float HalationProcessor::radiusInPixels(int imageHeight) const
{
    // 0.0 → 0 px, 1.0 → 3% of image height (two-Gaussian inner lobe)
    return mParams.radius * static_cast<float>(imageHeight) * 0.03f;
}

// ---------------------------------------------------------------------------
// GPU stubs (not yet implemented)
// ---------------------------------------------------------------------------

OfxStatus HalationProcessor::processHorizontalGPU(OfxImageEffectHandle,
                                                   OfxPropertySetHandle,
                                                   OfxPropertySetHandle) const
{
    // TODO: dispatch shaders/glsl/halation_h.glsl / shaders/metal/halation_h.metal
    return kOfxStatReplyDefault;
}

OfxStatus HalationProcessor::processVerticalGPU(OfxImageEffectHandle,
                                                 OfxPropertySetHandle,
                                                 OfxPropertySetHandle,
                                                 OfxPropertySetHandle) const
{
    // TODO: dispatch shaders/glsl/halation_v.glsl
    return kOfxStatReplyDefault;
}

// ---------------------------------------------------------------------------
// CPU path — full single-pass implementation
// ---------------------------------------------------------------------------

OfxStatus HalationProcessor::processCPU(const float* src, float* dst,
                                         int width, int height,
                                         int nComponents) const
{
    const int nRGB = 3; // haloSrc always RGB
    const int npix  = width * height;

    // ------------------------------------------------------------------
    // Step 1 & 2: build haloSrc = threshold(luma) * spectral_bias * src
    // ------------------------------------------------------------------
    std::vector<float> haloSrc(npix * nRGB);

    const float thLo = mParams.threshold - 0.05f;
    const float thHi = mParams.threshold + 0.05f;

    for (int i = 0; i < npix; ++i) {
        const float* s = src + i * nComponents;
        float r = s[0];
        float g = s[1];
        float b = s[2];

        float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float t    = smoothstep(thLo, thHi, luma);

        haloSrc[i * nRGB + 0] = r * t * mParams.biasR;
        haloSrc[i * nRGB + 1] = g * t * mParams.biasG;
        haloSrc[i * nRGB + 2] = b * t * mParams.biasB;
    }

    // ------------------------------------------------------------------
    // Step 3: dual-Gaussian separable blur
    // ------------------------------------------------------------------
    const float innerSigma = radiusInPixels(height);
    const float outerSigma = innerSigma * mParams.outerRadiusScale;

    // Guard against degenerate sigma (very small radius)
    const float minSigma = 0.5f;
    const float sigmaInner = std::max(innerSigma, minSigma);
    const float sigmaOuter = std::max(outerSigma, minSigma);

    int rInner, rOuter;
    std::vector<float> kernelInner = buildKernel(sigmaInner, rInner);
    std::vector<float> kernelOuter = buildKernel(sigmaOuter, rOuter);

    // Temporary buffers (RGB only)
    std::vector<float> tempH(npix * nRGB);
    std::vector<float> innerBlur(npix * nRGB);
    std::vector<float> tempH2(npix * nRGB);
    std::vector<float> outerBlur(npix * nRGB);

    // Inner Gaussian: haloSrc → tempH (horizontal) → innerBlur (vertical)
    blurH(haloSrc.data(), tempH.data(),     width, height, nRGB, kernelInner, rInner);
    blurV(tempH.data(),   innerBlur.data(), width, height, nRGB, kernelInner, rInner);

    // Outer Gaussian: haloSrc → tempH2 (horizontal) → outerBlur (vertical)
    blurH(haloSrc.data(), tempH2.data(),    width, height, nRGB, kernelOuter, rOuter);
    blurV(tempH2.data(),  outerBlur.data(), width, height, nRGB, kernelOuter, rOuter);

    // ------------------------------------------------------------------
    // Step 4: composite  dst = src + blendedBlur * intensity
    // ------------------------------------------------------------------
    const float wInner = 1.0f - mParams.outerWeight;
    const float wOuter = mParams.outerWeight;
    const float intensity = mParams.intensity;

    for (int i = 0; i < npix; ++i) {
        const float* s  = src + i * nComponents;
        float*       d  = dst + i * nComponents;

        for (int c = 0; c < nRGB; ++c) {
            float blended = wInner * innerBlur[i * nRGB + c]
                          + wOuter * outerBlur[i * nRGB + c];
            d[c] = std::max(0.0f, s[c] + blended * intensity);
        }

        // Alpha passthrough
        if (nComponents == 4)
            d[3] = s[3];
    }

    return kOfxStatOK;
}

} // namespace MasterFilm
