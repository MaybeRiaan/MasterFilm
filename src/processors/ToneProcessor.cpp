// src/processors/ToneProcessor.cpp
#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

float ToneProcessor::evaluateCurve(float x) const
{
    // Piecewise: black lift → toe → linear → shoulder → white clip
    // Parametric S-curve using smooth-step blends.

    // 1. Remap through black/white points
    float t = (x - mParams.blackPoint) / (mParams.whitePoint - mParams.blackPoint);
    t = std::clamp(t, 0.0f, 1.0f);

    // 2. Toe: smooth shadow rolloff
    // toe parameter [0,1] controls how far the toe extends
    float toeEdge = mParams.toe;
    if (t < toeEdge) {
        float tn = t / toeEdge;
        t = toeEdge * (tn * tn * (3.0f - 2.0f * tn));
    }

    // 3. Shoulder: smooth highlight rolloff
    float shoulderEdge = mParams.shoulder;
    if (t > shoulderEdge) {
        float sn = (t - shoulderEdge) / (1.0f - shoulderEdge);
        float shaped = sn * sn * (3.0f - 2.0f * sn);
        t = shoulderEdge + (1.0f - shoulderEdge) * shaped;
    }

    // 4. Mid-gamma (applied in the linear region between toe and shoulder)
    if (t >= toeEdge && t <= shoulderEdge) {
        float mid = (t - toeEdge) / (shoulderEdge - toeEdge);
        mid = std::pow(mid, 1.0f / mParams.midGamma);
        t = toeEdge + mid * (shoulderEdge - toeEdge);
    }

    return std::clamp(t, 0.0f, 1.0f);
}

void ToneProcessor::rebuildLUT()
{
    for (int i = 0; i < kLUTSize; ++i) {
        float x = static_cast<float>(i) / static_cast<float>(kLUTSize - 1);
        mLUT[i] = evaluateCurve(x);
    }
}

OfxStatus ToneProcessor::processGPU(OfxImageEffectHandle, OfxPropertySetHandle, OfxPropertySetHandle) const
{
    // TODO: upload mLUT as a 1D texture sampler, dispatch shaders/glsl/tone.glsl
    return kOfxStatReplyDefault;
}

OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
                                    int width, int height, int nComponents) const
{
    auto sample = [&](float v) -> float {
        float fi = std::clamp(v, 0.0f, 1.0f) * (kLUTSize - 1);
        int lo = static_cast<int>(fi);
        int hi = std::min(lo + 1, kLUTSize - 1);
        float frac = fi - static_cast<float>(lo);
        return mLUT[lo] * (1.0f - frac) + mLUT[hi] * frac;
    };

    int n = width * height * nComponents;
    // Apply LUT to R, G, B channels; pass alpha through
    for (int i = 0; i < n; ++i) {
        int c = i % nComponents;
        dst[i] = (nComponents == 4 && c == 3) ? src[i] : sample(src[i]);
    }
    return kOfxStatOK;
}

} // namespace MasterFilm
