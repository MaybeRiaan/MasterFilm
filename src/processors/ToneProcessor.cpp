// src/processors/ToneProcessor.cpp
#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    float ToneProcessor::evaluateCurve(float x) const
    {
        // Piecewise S-curve: black/white remap → toe rolloff → shoulder rolloff → gamma
        // All in normalised scene space [0, 1].

        // 1. Remap input through black/white points
        float t = (x - mParams.blackPoint) / (mParams.whitePoint - mParams.blackPoint);
        t = std::clamp(t, 0.0f, 1.0f);

        const float toeEdge = mParams.toe;
        const float shoulderEdge = mParams.shoulder;

        // 2. Toe: smooth shadow rolloff (smoothstep in the toe region)
        if (t < toeEdge) {
            float tn = t / toeEdge;
            t = toeEdge * (tn * tn * (3.0f - 2.0f * tn));
        }
        // 3. Shoulder: smooth highlight rolloff
        else if (t > shoulderEdge) {
            float sn = (t - shoulderEdge) / (1.0f - shoulderEdge);
            float shaped = sn * sn * (3.0f - 2.0f * sn);
            t = shoulderEdge + (1.0f - shoulderEdge) * shaped;
        }
        // 4. Mid-gamma in the linear region between toe and shoulder
        else {
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

    // ── LUT sampler (inlined for hot-path use) ────────────────────────────────────
    // Clamps input to [0,1] then bilinearly interpolates the 1D LUT.
    inline float ToneProcessor::sampleLUT(float v) const
    {
        float fi = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(kLUTSize - 1);
        int   lo = static_cast<int>(fi);
        int   hi = lo + 1;
        if (hi >= kLUTSize) hi = kLUTSize - 1;
        float frac = fi - static_cast<float>(lo);
        return mLUT[lo] + frac * (mLUT[hi] - mLUT[lo]);
    }

    OfxStatus ToneProcessor::processGPU(OfxImageEffectHandle,
        OfxPropertySetHandle,
        OfxPropertySetHandle) const
    {
        // TODO: upload mLUT as a 1D texture sampler, dispatch shaders/glsl/tone.glsl
        return kOfxStatReplyDefault;
    }

    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents) const
    {
        // Walk pixel by pixel. For each pixel, apply the LUT to R, G, B and copy
        // alpha unchanged. Avoids any per-channel modulo arithmetic in the hot path.
        //
        // nComponents is always 4 (RGBA float) in practice, but we branch on it
        // cleanly so the function stays correct if called with RGB (3) buffers.

        const int nPixels = width * height;

        if (nComponents == 4)
        {
            // Fast path: RGBA — unrolled, no branching inside the pixel loop.
            const float* s = src;
            float* d = dst;
            for (int p = 0; p < nPixels; ++p, s += 4, d += 4)
            {
                d[0] = sampleLUT(s[0]);   // R
                d[1] = sampleLUT(s[1]);   // G
                d[2] = sampleLUT(s[2]);   // B
                d[3] = s[3];              // A — pass through unchanged
            }
        }
        else if (nComponents == 3)
        {
            // RGB path (no alpha channel)
            const float* s = src;
            float* d = dst;
            for (int p = 0; p < nPixels; ++p, s += 3, d += 3)
            {
                d[0] = sampleLUT(s[0]);
                d[1] = sampleLUT(s[1]);
                d[2] = sampleLUT(s[2]);
            }
        }
        else
        {
            // Generic fallback — apply LUT to all channels except the last in a 4+
            // component buffer (treat last as alpha). Rarely hit.
            const int alphaChannel = (nComponents == 4) ? 3 : -1;
            const float* s = src;
            float* d = dst;
            for (int p = 0; p < nPixels; ++p)
            {
                for (int c = 0; c < nComponents; ++c, ++s, ++d)
                    *d = (c == alphaChannel) ? *s : sampleLUT(*s);
            }
        }

        return kOfxStatOK;
    }

} // namespace MasterFilm