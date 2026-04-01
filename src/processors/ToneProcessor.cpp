// src/processors/ToneProcessor.cpp
#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    // ── Curve evaluation ──────────────────────────────────────────────────────────
    // x is scene linear, range [0, kLinearMax].
    // Output is normalised density [0, 1].
    //
    // Steps:
    //   1. Remap through blackPoint / whitePoint → normalised t in [0,1]
    //   2. Toe smoothstep in shadow region
    //   3. Shoulder smoothstep in highlight region
    //   4. Mid-gamma power in the straight-line region between toe and shoulder
    float ToneProcessor::evaluateCurve(float x) const
    {
        // 1. Remap through black/white points
        float t = (x - mParams.blackPoint) / (mParams.whitePoint - mParams.blackPoint);
        t = std::clamp(t, 0.0f, 1.0f);

        const float toeEdge = mParams.toe;
        const float shoulderEdge = mParams.shoulder;

        // 2. Toe — smooth shadow rolloff
        if (t < toeEdge)
        {
            float tn = t / toeEdge;
            t = toeEdge * (tn * tn * (3.0f - 2.0f * tn));
        }
        // 3. Shoulder — smooth highlight rolloff
        else if (t > shoulderEdge)
        {
            float sn = (t - shoulderEdge) / (1.0f - shoulderEdge);
            float shaped = sn * sn * (3.0f - 2.0f * sn);
            t = shoulderEdge + (1.0f - shoulderEdge) * shaped;
        }
        // 4. Mid-gamma — straight-line region
        else
        {
            float mid = (t - toeEdge) / (shoulderEdge - toeEdge);
            mid = std::pow(mid, 1.0f / mParams.midGamma);
            t = toeEdge + mid * (shoulderEdge - toeEdge);
        }

        return std::clamp(t, 0.0f, 1.0f);
    }

    // ── LUT bake ──────────────────────────────────────────────────────────────────
    // Entry i maps scene linear value (i / (kLUTSize-1)) * kLinearMax → density.
    // kLinearMax = 16.0 covers ~+6.5 stops above diffuse white — enough for any
    // real scene. Values above kLinearMax are clamped to the shoulder output.
    void ToneProcessor::rebuildLUT()
    {
        for (int i = 0; i < kLUTSize; ++i)
        {
            float linearVal = (static_cast<float>(i) / static_cast<float>(kLUTSize - 1))
                * kLinearMax;
            mLUT[i] = evaluateCurve(linearVal);
        }
    }

    // ── LUT sampler ───────────────────────────────────────────────────────────────
    // Converts a scene linear value to a [0,1] LUT index, then bilinear interpolates.
    // Values outside [0, kLinearMax] are clamped to the LUT endpoints.
    inline float ToneProcessor::sampleLUT(float linearVal) const
    {
        // Normalise to LUT index space
        float fi = (std::clamp(linearVal, 0.0f, kLinearMax) / kLinearMax)
            * static_cast<float>(kLUTSize - 1);

        int   lo = static_cast<int>(fi);
        int   hi = lo + 1;
        if (hi >= kLUTSize) hi = kLUTSize - 1;

        float frac = fi - static_cast<float>(lo);
        return mLUT[lo] + frac * (mLUT[hi] - mLUT[lo]);
    }

    // ── CPU processing ────────────────────────────────────────────────────────────
    // Per pixel:
    //   1. Forward CST: encoded working space → scene linear
    //   2. LUT sample: scene linear → normalised density [0,1]
    //   3. Inverse CST: normalised density → encoded working space
    //
    // Called once per row (height=1) from onRender so stride handling stays
    // in the caller. Alpha channel (index 3) is always passed through.
    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents,
        ColorSpaceMode mode) const
    {
        const int nPixels = width * height;

        if (nComponents == 4)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 4, d += 4)
            {
                // Forward CST → scene linear
                float r = CST::toLinear(s[0], mode);
                float g = CST::toLinear(s[1], mode);
                float b = CST::toLinear(s[2], mode);

                // Tone curve in linear light
                r = sampleLUT(r);
                g = sampleLUT(g);
                b = sampleLUT(b);

                // Inverse CST → working color space
                d[0] = CST::fromLinear(r, mode);
                d[1] = CST::fromLinear(g, mode);
                d[2] = CST::fromLinear(b, mode);
                d[3] = s[3];  // alpha unchanged
            }
        }
        else if (nComponents == 3)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 3, d += 3)
            {
                d[0] = CST::fromLinear(sampleLUT(CST::toLinear(s[0], mode)), mode);
                d[1] = CST::fromLinear(sampleLUT(CST::toLinear(s[1], mode)), mode);
                d[2] = CST::fromLinear(sampleLUT(CST::toLinear(s[2], mode)), mode);
            }
        }

        return kOfxStatOK;
    }

    OfxStatus ToneProcessor::processGPU(OfxImageEffectHandle,
        OfxPropertySetHandle,
        OfxPropertySetHandle) const
    {
        // TODO: upload mLUT as 1D texture, dispatch tone.glsl with CST uniforms
        return kOfxStatReplyDefault;
    }

} // namespace MasterFilm