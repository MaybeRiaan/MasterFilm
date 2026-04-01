// src/processors/ToneProcessor.cpp
#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    // ── Curve evaluation ──────────────────────────────────────────────────────────
    // x        — scene linear input (0.18 = middle grey)
    // returns  — normalised [0,1] where 1.0 corresponds to whitePoint linear
    //
    // Three regions defined by absolute scene linear input values:
    //   Toe:      [blackPoint, toeIn]      — smoothstep 0 → toeOut
    //   Straight: [toeIn, shoulderIn]      — linear with midGamma, toeOut → shoulderOut
    //   Shoulder: [shoulderIn, whitePoint] — smoothstep shoulderOut → 1.0
    //   Above wp: clamped to 1.0
    float ToneProcessor::evaluateCurve(float x) const
    {
        const float bp = mParams.blackPoint;
        const float ti = mParams.toeIn;
        const float si = mParams.shoulderIn;
        const float wp = mParams.whitePoint;
        const float to = mParams.toeOut;
        const float so = mParams.shoulderOut;

        if (x <= bp)
            return 0.0f;

        // Toe — smoothstep from 0 to toeOut
        if (x < ti)
        {
            float tn = (x - bp) / (ti - bp);
            tn = std::clamp(tn, 0.0f, 1.0f);
            return to * (tn * tn * (3.0f - 2.0f * tn));
        }

        // Shoulder — smoothstep from shoulderOut to 1.0
        if (x >= si)
        {
            float sn = (x - si) / (wp - si);
            sn = std::clamp(sn, 0.0f, 1.0f);
            float shaped = sn * sn * (3.0f - 2.0f * sn);
            return so + (1.0f - so) * shaped;
        }

        // Straight-line — midGamma applied, toeOut → shoulderOut
        float t = (x - ti) / (si - ti);
        t = std::clamp(t, 0.0f, 1.0f);
        t = std::pow(t, 1.0f / mParams.midGamma);
        return to + t * (so - to);
    }

    // ── LUT bake ──────────────────────────────────────────────────────────────────
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
    inline float ToneProcessor::sampleLUT(float linearVal) const
    {
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
    //   2. LUT: scene linear → normalised [0,1] density output
    //   3. Scale: normalised output × whitePoint → scene linear
    //      (LUT output of 1.0 corresponds to whitePoint in scene linear)
    //   4. Inverse CST: scene linear → encoded working space
    //
    // Step 3 is critical — the LUT outputs a normalised value, not a scene
    // linear value. The inverse CST expects scene linear, so we must rescale
    // before passing to fromLinear(). Without this the inverse CST receives
    // values in [0,1] and interprets them as very dark scene linear values,
    // producing heavily compressed and banded output.
    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents,
        ColorSpaceMode mode) const
    {
        const int   nPixels = width * height;
        const float wpScale = mParams.whitePoint;  // scale factor for step 3

        if (nComponents == 4)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 4, d += 4)
            {
                // 1. Forward CST → scene linear
                float r = CST::toLinear(s[0], mode);
                float g = CST::toLinear(s[1], mode);
                float b = CST::toLinear(s[2], mode);

                // 2. Tone curve → normalised [0,1]
                r = sampleLUT(r);
                g = sampleLUT(g);
                b = sampleLUT(b);

                // 3. Scale back to scene linear
                r *= wpScale;
                g *= wpScale;
                b *= wpScale;

                // 4. Inverse CST → working color space
                d[0] = CST::fromLinear(r, mode);
                d[1] = CST::fromLinear(g, mode);
                d[2] = CST::fromLinear(b, mode);
                d[3] = s[3];
            }
        }
        else if (nComponents == 3)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 3, d += 3)
            {
                d[0] = CST::fromLinear(sampleLUT(CST::toLinear(s[0], mode)) * wpScale, mode);
                d[1] = CST::fromLinear(sampleLUT(CST::toLinear(s[1], mode)) * wpScale, mode);
                d[2] = CST::fromLinear(sampleLUT(CST::toLinear(s[2], mode)) * wpScale, mode);
            }
        }

        return kOfxStatOK;
    }

    OfxStatus ToneProcessor::processGPU(OfxImageEffectHandle,
        OfxPropertySetHandle,
        OfxPropertySetHandle) const
    {
        return kOfxStatReplyDefault;
    }

} // namespace MasterFilm