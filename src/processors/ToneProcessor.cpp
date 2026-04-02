// src/processors/ToneProcessor.cpp
// Per-channel H&D characteristic curve with scan encoding.
//
// Signal flow per pixel:
//   1. Forward CST: encoded → scene linear
//   2. Log exposure: scene linear → log2 stops relative to 0.18
//   3. H&D curve: log exposure → density (per-channel LUT)
//   4. Film color blend: lerp R/B density toward G density
//   5. Scan encode: density → code value (single scale, per-channel anchor)
//
// The scale is derived from the green channel's straight-line region
// and applied to all channels equally. Colour separation comes from
// each channel producing different density values at the same exposure.

#include "ToneProcessor.h"
#include <cmath>
#include <algorithm>

namespace MasterFilm {

    // ── H&D curve evaluation ──────────────────────────────────────────────────────
    float ToneProcessor::evaluateCurve(float logExp, const ChannelCurve& c) const
    {
        const float straightSpan = c.shoulderStops - c.toeEndStops;
        const float dToeEnd   = c.dMin + c.gamma * (c.toeEndStops - c.toeStartStops) * 0.3f;
        const float dShoulder = dToeEnd + c.gamma * straightSpan;
        const float dShoulderClamped = std::min(dShoulder, c.dMax);

        if (logExp <= c.toeStartStops)
            return c.dMin;

        // Toe — smoothstep from dMin to dToeEnd
        if (logExp < c.toeEndStops)
        {
            float t = (logExp - c.toeStartStops) / (c.toeEndStops - c.toeStartStops);
            t = std::clamp(t, 0.0f, 1.0f);
            float shaped = t * t * (3.0f - 2.0f * t);
            return c.dMin + (dToeEnd - c.dMin) * shaped;
        }

        // Shoulder — smoothstep from dShoulder to dMax
        if (logExp >= c.shoulderStops)
        {
            if (logExp >= c.clipStops)
                return c.dMax;

            float t = (logExp - c.shoulderStops) / (c.clipStops - c.shoulderStops);
            t = std::clamp(t, 0.0f, 1.0f);
            float shaped = t * t * (3.0f - 2.0f * t);
            return dShoulderClamped + (c.dMax - dShoulderClamped) * shaped;
        }

        // Straight line — gamma density per stop
        float stopsFromToeEnd = logExp - c.toeEndStops;
        return dToeEnd + c.gamma * stopsFromToeEnd;
    }

    // ── LUT bake ──────────────────────────────────────────────────────────────────
    void ToneProcessor::rebuildLUTs()
    {
        for (int i = 0; i < kLUTSize; ++i)
        {
            float logExp = kStopsMin + (static_cast<float>(i) / static_cast<float>(kLUTSize - 1))
                         * kStopsRange;

            mLUT_R[i] = evaluateCurve(logExp, mParams.red);
            mLUT_G[i] = evaluateCurve(logExp, mParams.green);
            mLUT_B[i] = evaluateCurve(logExp, mParams.blue);
        }

        // Density at middle grey (0 stops) — per-channel anchors
        mDMidR = evaluateCurve(0.0f, mParams.red);
        mDMidG = evaluateCurve(0.0f, mParams.green);
        mDMidB = evaluateCurve(0.0f, mParams.blue);

        // Green density at shoulder onset — for scale derivation
        mDHighG = evaluateCurve(mParams.green.shoulderStops, mParams.green);
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

    // ── Scan encoding ─────────────────────────────────────────────────────────────
    float ToneProcessor::scanEncode(float density, float dMid,
                                    float codeMidGrey, float scale)
    {
        return codeMidGrey + (density - dMid) * scale;
    }

    // ── Code value targets ────────────────────────────────────────────────────────

    float ToneProcessor::getCodeMidGrey(ColorSpaceMode mode)
    {
        switch (mode)
        {
        case ColorSpaceMode::ACEScct:          return 0.4135f;
        case ColorSpaceMode::DaVinciWideGamut: return 0.5f;
        }
        return 0.4135f;
    }

    // Where the green channel's shoulder onset should land in code values.
    // This is the primary tuning value for highlight brightness.
    //
    // ACEScct reference points:
    //   0.4135  middle grey (18%)
    //   0.525   scene linear 1.0 (diffuse white, ~2.5 stops above grey)
    //   0.570   scene linear 2.0 (~3.5 stops above grey)
    //   0.600   scene linear 4.0 (~4.5 stops above grey)
    //
    // Green shoulder onset is at +6.5 stops. We want that to land
    // well above diffuse white to preserve highlight latitude.
    float ToneProcessor::getCodeHighlight(ColorSpaceMode mode)
    {
        switch (mode)
        {
        case ColorSpaceMode::ACEScct:          return 0.60f;
        case ColorSpaceMode::DaVinciWideGamut: return 0.70f;
        }
        return 0.60f;
    }

    // ── CPU processing ────────────────────────────────────────────────────────────
    OfxStatus ToneProcessor::processCPU(const float* src, float* dst,
        int width, int height,
        int nComponents,
        ColorSpaceMode mode) const
    {
        const int   nPixels = width * height;
        const float filmColor = mParams.filmColor;

        const float codeMidGrey   = getCodeMidGrey(mode);
        const float codeHighlight = getCodeHighlight(mode);

        // Single scale derived from green channel's straight-line region
        const float scale = (codeHighlight - codeMidGrey) / (mDHighG - mDMidG);

        const float dMidG = mDMidG;

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

                // 2. Scene linear → log2 stops relative to middle grey
                constexpr float kFloor = 1e-10f;
                constexpr float kMiddleGrey = 0.18f;
                float rStops = std::log2(std::max(rLin, kFloor) / kMiddleGrey);
                float gStops = std::log2(std::max(gLin, kFloor) / kMiddleGrey);
                float bStops = std::log2(std::max(bLin, kFloor) / kMiddleGrey);

                // 3. H&D curve → density (per-channel LUT)
                float rDensity = sampleLUT(rStops, mLUT_R);
                float gDensity = sampleLUT(gStops, mLUT_G);
                float bDensity = sampleLUT(bStops, mLUT_B);

                // 4. Film color blend
                float dMidR = mDMidR;
                float dMidB = mDMidB;

                if (filmColor < 1.0f)
                {
                    float gDensityForR = sampleLUT(rStops, mLUT_G);
                    float gDensityForB = sampleLUT(bStops, mLUT_G);
                    rDensity = gDensityForR + filmColor * (rDensity - gDensityForR);
                    bDensity = gDensityForB + filmColor * (bDensity - gDensityForB);

                    dMidR = dMidG + filmColor * (mDMidR - dMidG);
                    dMidB = dMidG + filmColor * (mDMidB - dMidG);
                }

                // 5. Scan encode — same scale, per-channel anchor
                d[0] = scanEncode(rDensity, dMidR, codeMidGrey, scale);
                d[1] = scanEncode(gDensity, dMidG, codeMidGrey, scale);
                d[2] = scanEncode(bDensity, dMidB, codeMidGrey, scale);
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

                float rDensity = sampleLUT(rStops, mLUT_R);
                float gDensity = sampleLUT(gStops, mLUT_G);
                float bDensity = sampleLUT(bStops, mLUT_B);

                float dMidR = mDMidR;
                float dMidB = mDMidB;

                if (filmColor < 1.0f)
                {
                    float gDensityForR = sampleLUT(rStops, mLUT_G);
                    float gDensityForB = sampleLUT(bStops, mLUT_G);
                    rDensity = gDensityForR + filmColor * (rDensity - gDensityForR);
                    bDensity = gDensityForB + filmColor * (bDensity - gDensityForB);

                    dMidR = dMidG + filmColor * (mDMidR - dMidG);
                    dMidB = dMidG + filmColor * (mDMidB - dMidG);
                }

                d[0] = scanEncode(rDensity, dMidR, codeMidGrey, scale);
                d[1] = scanEncode(gDensity, dMidG, codeMidGrey, scale);
                d[2] = scanEncode(bDensity, dMidB, codeMidGrey, scale);
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
