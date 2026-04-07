// src/processors/ToneProcessor.cpp
// Per-channel H&D characteristic curve with full curve inverse.
//
// Signal flow per pixel:
//   1. Forward CST: encoded → scene linear
//   2. Log exposure: scene linear → log2 stops relative to 0.18
//   3. Film transfer per channel:
//      a. H&D curve: stops → density (forward)
//      b. Full inverse: density → recovered stops (inverts all regions)
//      c. Equivalent linear: 0.18 × 2^recoveredStops
//   4. Film color blend: lerp R/B equivalent linear toward G
//   5. Inverse CST: equivalent linear → encoded output
//
// FULL CURVE INVERSE
// ─────────────────────────────────────────────────────────────────────────────
// The forward curve has three regions:
//   Toe:      smoothstep from dMin to dToeEnd      (toeStart..toeEnd stops)
//   Straight: linear at gamma slope                 (toeEnd..shoulder stops)
//   Shoulder: smoothstep from dShoulder to dMax     (shoulder..clip stops)
//
// The inverse maps density back to stops by detecting which region the
// density falls in and inverting that region's function:
//
//   density <= dMin      → toeStartStops (true black)
//   density < dToeEnd    → inverse smoothstep in toe region
//   density < dShoulder  → linear inverse: (density - dToeEnd) / gamma + toeEnd
//   density < dMax       → inverse smoothstep in shoulder region
//   density >= dMax      → clipStops
//
// The inverse smoothstep uses the analytical inverse of t²(3-2t):
//   t = 0.5 - sin(asin(1 - 2*y) / 3)
// where y is the normalised density within the region.

#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    // ── Curve geometry ────────────────────────────────────────────────────────────
    ToneProcessor::CurveGeometry ToneProcessor::computeGeometry(const ChannelCurve& c)
    {
        CurveGeometry g;
        g.dToeEnd   = c.dMin + c.gamma * (c.toeEndStops - c.toeStartStops) * 0.3f;
        g.dShoulder = g.dToeEnd + c.gamma * (c.shoulderStops - c.toeEndStops);
        g.dShoulderClamped = std::min(g.dShoulder, c.dMax);
        return g;
    }

    // ── Forward H&D curve ─────────────────────────────────────────────────────────
    float ToneProcessor::evaluateCurve(float logExp, const ChannelCurve& c) const
    {
        const CurveGeometry* g;
        // Select the right pre-computed geometry
        // (We can't easily pass it, so recompute — it's only used in LUT bake)
        CurveGeometry local = computeGeometry(c);
        g = &local;

        if (logExp <= c.toeStartStops)
            return c.dMin;

        // Toe — smoothstep from dMin to dToeEnd
        if (logExp < c.toeEndStops)
        {
            float t = (logExp - c.toeStartStops) / (c.toeEndStops - c.toeStartStops);
            t = std::clamp(t, 0.0f, 1.0f);
            float shaped = t * t * (3.0f - 2.0f * t);
            return c.dMin + (g->dToeEnd - c.dMin) * shaped;
        }

        // Shoulder — smoothstep from dShoulder to dMax
        if (logExp >= c.shoulderStops)
        {
            if (logExp >= c.clipStops)
                return c.dMax;

            float t = (logExp - c.shoulderStops) / (c.clipStops - c.shoulderStops);
            t = std::clamp(t, 0.0f, 1.0f);
            float shaped = t * t * (3.0f - 2.0f * t);
            return g->dShoulderClamped + (c.dMax - g->dShoulderClamped) * shaped;
        }

        // Straight line
        return g->dToeEnd + c.gamma * (logExp - c.toeEndStops);
    }

    // ── Inverse smoothstep ────────────────────────────────────────────────────────
    // Analytical inverse of y = t²(3-2t) for y in [0,1], returns t in [0,1].
    // Uses the identity: inverse smoothstep = 0.5 - sin(asin(1-2y) / 3)
    static float inverseSmoothstep(float y)
    {
        y = std::clamp(y, 0.0f, 1.0f);
        if (y <= 0.0f) return 0.0f;
        if (y >= 1.0f) return 1.0f;
        return 0.5f - std::sin(std::asin(1.0f - 2.0f * y) / 3.0f);
    }

    // ── Full curve inverse ────────────────────────────────────────────────────────
    // density → original stops
    // Inverts each region of the forward curve separately.
    float ToneProcessor::inverseCurve(float density, const ChannelCurve& c) const
    {
        CurveGeometry g = computeGeometry(c);

        // At or below base fog → toeStart (deepest shadow)
        if (density <= c.dMin)
            return c.toeStartStops;

        // Toe region: density in [dMin, dToeEnd]
        if (density < g.dToeEnd)
        {
            // Forward: density = dMin + (dToeEnd - dMin) * smoothstep(t)
            // where t = (stops - toeStart) / (toeEnd - toeStart)
            float y = (density - c.dMin) / (g.dToeEnd - c.dMin);
            float t = inverseSmoothstep(y);
            return c.toeStartStops + t * (c.toeEndStops - c.toeStartStops);
        }

        // Shoulder region: density in [dShoulder, dMax]
        if (density >= g.dShoulderClamped)
        {
            if (density >= c.dMax)
                return c.clipStops;

            // Forward: density = dShoulder + (dMax - dShoulder) * smoothstep(t)
            // where t = (stops - shoulder) / (clip - shoulder)
            float y = (density - g.dShoulderClamped) / (c.dMax - g.dShoulderClamped);
            float t = inverseSmoothstep(y);
            return c.shoulderStops + t * (c.clipStops - c.shoulderStops);
        }

        // Straight line: density = dToeEnd + gamma * (stops - toeEnd)
        return c.toeEndStops + (density - g.dToeEnd) / c.gamma;
    }

    // ── Film transfer ─────────────────────────────────────────────────────────────
    // stops → density → inverse → recovered stops → equivalent linear
    //
    // In the straight-line region: identity (input stops = output stops).
    // In the toe: compression → shadows darken.
    // In the shoulder: compression → highlights roll off.
    // At dMin: returns toeStartStops → very small linear value → near black.
    float ToneProcessor::filmTransfer(float stops, const ChannelCurve& curve) const
    {
        float density = evaluateCurve(stops, curve);
        float recoveredStops = inverseCurve(density, curve);

        constexpr float kMiddleGrey = 0.18f;
        float equivalentLinear = kMiddleGrey * std::pow(2.0f, recoveredStops);

        return std::max(equivalentLinear, 0.0f);
    }

    // ── LUT bake ──────────────────────────────────────────────────────────────────
    void ToneProcessor::rebuildLUTs()
    {
        // Pre-compute curve geometry
        mGeomR = computeGeometry(mParams.red);
        mGeomG = computeGeometry(mParams.green);
        mGeomB = computeGeometry(mParams.blue);

        for (int i = 0; i < kLUTSize; ++i)
        {
            float stops = kStopsMin + (static_cast<float>(i) / static_cast<float>(kLUTSize - 1))
                        * kStopsRange;

            mLUT_R[i] = filmTransfer(stops, mParams.red);
            mLUT_G[i] = filmTransfer(stops, mParams.green);
            mLUT_B[i] = filmTransfer(stops, mParams.blue);
        }
    }

    // ── LUT sampler ───────────────────────────────────────────────────────────────
    float ToneProcessor::sampleLUT(float logExp,
                                   const std::array<float, kLUTSize>& lut) const
    {
        float normalised = (std::clamp(logExp, kStopsMin, kStopsMax) - kStopsMin) / kStopsRange;
        float fi = normalised * static_cast<float>(kLUTSize - 1);

        int lo = static_cast<int>(fi);
        int hi = lo + 1;
        if (hi >= kLUTSize) hi = kLUTSize - 1;

        float frac = fi - static_cast<float>(lo);
        return lut[lo] + frac * (lut[hi] - lut[lo]);
    }

    // ── CPU processing ────────────────────────────────────────────────────────────
    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents,
        ColorSpaceMode mode) const
    {
        const int   nPixels = width * height;
        const float filmColor = mParams.filmColor;

        if (nComponents == 4)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 4, d += 4)
            {
                // 1. Forward CST → scene linear
                float rLin = CST::toLinear(s[0], mode);
                float gLin = CST::toLinear(s[1], mode);
                float bLin = CST::toLinear(s[2], mode);

                // 2. Scene linear → log2 stops
                constexpr float kFloor = 1e-10f;
                constexpr float kMiddleGrey = 0.18f;
                float rStops = std::log2(std::max(rLin, kFloor) / kMiddleGrey);
                float gStops = std::log2(std::max(gLin, kFloor) / kMiddleGrey);
                float bStops = std::log2(std::max(bLin, kFloor) / kMiddleGrey);

                // 3. Film transfer → equivalent linear (per-channel LUT)
                float rOut = sampleLUT(rStops, mLUT_R);
                float gOut = sampleLUT(gStops, mLUT_G);
                float bOut = sampleLUT(bStops, mLUT_B);

                // 4. Film color blend — lerp R/B toward G
                if (filmColor < 1.0f)
                {
                    float gOutForR = sampleLUT(rStops, mLUT_G);
                    float gOutForB = sampleLUT(bStops, mLUT_G);
                    rOut = gOutForR + filmColor * (rOut - gOutForR);
                    bOut = gOutForB + filmColor * (bOut - gOutForB);
                }

                // 5. Inverse CST → encoded output
                d[0] = CST::fromLinear(rOut, mode);
                d[1] = CST::fromLinear(gOut, mode);
                d[2] = CST::fromLinear(bOut, mode);
                d[3] = s[3];
            }
        }
        else if (nComponents == 3)
        {
            const float* s = src;
            float* d = dst;

            for (int p = 0; p < nPixels; ++p, s += 3, d += 3)
            {
                constexpr float kFloor = 1e-10f;
                constexpr float kMiddleGrey = 0.18f;

                float rLin = CST::toLinear(s[0], mode);
                float gLin = CST::toLinear(s[1], mode);
                float bLin = CST::toLinear(s[2], mode);

                float rStops = std::log2(std::max(rLin, kFloor) / kMiddleGrey);
                float gStops = std::log2(std::max(gLin, kFloor) / kMiddleGrey);
                float bStops = std::log2(std::max(bLin, kFloor) / kMiddleGrey);

                float rOut = sampleLUT(rStops, mLUT_R);
                float gOut = sampleLUT(gStops, mLUT_G);
                float bOut = sampleLUT(bStops, mLUT_B);

                if (filmColor < 1.0f)
                {
                    float gOutForR = sampleLUT(rStops, mLUT_G);
                    float gOutForB = sampleLUT(bStops, mLUT_G);
                    rOut = gOutForR + filmColor * (rOut - gOutForR);
                    bOut = gOutForB + filmColor * (bOut - gOutForB);
                }

                d[0] = CST::fromLinear(rOut, mode);
                d[1] = CST::fromLinear(gOut, mode);
                d[2] = CST::fromLinear(bOut, mode);
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
