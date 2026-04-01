// src/processors/ColorSpaceTransform.h
// Analytical color space transfer functions for MasterFilm's internal CST.
//
// All functions convert to/from scene linear light, normalised so that
// 0.18 = 18% grey (middle grey). Values above 1.0 are valid and expected —
// specular highlights and light sources routinely exceed 1.0 in scene linear.
//
// Sources:
//   ACEScct  — ACES specification S-2014-003 (Academy)
//   DaVinci Intermediate — Blackmagic Design DaVinci Intermediate white paper
//   Rec.709  — ITU-R BT.709, IEC 61966-2-1 (sRGB EOTF approximation)
//
// All functions are pure inline — no state, no heap, no dependencies.
// Safe to call from any thread.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cmath>
#include "../presets/FilmPreset.h"   // ColorSpaceMode

namespace MasterFilm {
namespace CST {

// =============================================================================
//  ACEScct  ↔  Scene Linear (AP1 primaries)
//  Source: Academy S-2014-003
// =============================================================================

// ACEScct encoded value → scene linear
// Piecewise: linear segment for deep shadows, log segment above cut
inline float acesCCT_to_linear(float v)
{
    // Constants from S-2014-003
    constexpr float CUT2  = 0.155251141552511f;  // encoded value below which linear segment applies
    constexpr float A     = 10.5402377416672f;
    constexpr float B     = 0.0729055341958355f;  // linear segment intercept (encoded black floor)
    constexpr float C     = 0.0570776999f;        // linear segment slope reciprocal term

    if (v <= CUT2)
    {
        // Linear segment: very deep shadows
        return (v - B) / A;
    }
    else
    {
        // Log segment: (2^((v - 0.413088) / 0.057077) - 2^-15) / 17.52
        return (std::pow(2.0f, (v - 0.413088f) / C) - std::pow(2.0f, -15.0f)) / 17.52f;
    }
}

// Scene linear → ACEScct encoded value
inline float linear_to_acesCCT(float v)
{
    constexpr float CUT1  = 0.0078125f;           // linear value below which linear segment applies
    constexpr float A     = 10.5402377416672f;
    constexpr float B     = 0.0729055341958355f;
    constexpr float C     = 0.0570776999f;

    if (v <= CUT1)
    {
        // Linear segment
        return A * v + B;
    }
    else
    {
        // Log segment: 0.057077 * log2(v * 17.52 + 2^-15) + 0.413088
        return C * std::log2(v * 17.52f + std::pow(2.0f, -15.0f)) + 0.413088f;
    }
}

// =============================================================================
//  DaVinci Intermediate  ↔  Scene Linear
//  Source: Blackmagic Design DaVinci Intermediate white paper (2021)
// =============================================================================

// DaVinci Intermediate encoded value → scene linear
inline float dwg_to_linear(float v)
{
    // Constants from Blackmagic white paper
    constexpr float A  = 0.0075f;
    constexpr float B  = 7.0f;
    constexpr float C  = 0.07329248f;
    constexpr float M  = 10.44426855f;
    constexpr float LIN_CUT = 0.00262409f;   // linear side cut point
    constexpr float LOG_CUT = 0.02740668f;   // log side cut point

    if (v <= LOG_CUT)
    {
        // Linear segment
        return (v - C) / M;
    }
    else
    {
        // Exponential segment
        return std::pow(B, v - C) / A - A;
    }
}

// Scene linear → DaVinci Intermediate encoded value
inline float linear_to_dwg(float v)
{
    constexpr float A  = 0.0075f;
    constexpr float B  = 7.0f;
    constexpr float C  = 0.07329248f;
    constexpr float M  = 10.44426855f;
    constexpr float LIN_CUT = 0.00262409f;

    if (v <= LIN_CUT)
    {
        // Linear segment
        return M * v + C;
    }
    else
    {
        // Log segment
        return std::log2((v + A) * A) / std::log2(B) + C;
    }
}

// =============================================================================
//  Rec.709 / DaVinci YRGB  ↔  Scene Linear
//  ITU-R BT.709 OETF inverse (display gamma 2.4 approximation)
//  Note: strictly BT.709 uses a piecewise with a linear toe, but for
//  scene-referred grading in Resolve YRGB the simple power law is correct.
// =============================================================================

// Rec.709 gamma-encoded value → scene linear
inline float rec709_to_linear(float v)
{
    if (v < 0.0f) return 0.0f;
    // BT.709 piecewise EOTF
    constexpr float THRESH = 0.081f;
    if (v < THRESH)
        return v / 4.5f;
    else
        return std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
}

// Scene linear → Rec.709 gamma-encoded value
inline float linear_to_rec709(float v)
{
    if (v < 0.0f) return 0.0f;
    // BT.709 piecewise OETF
    constexpr float THRESH = 0.018f;
    if (v < THRESH)
        return 4.5f * v;
    else
        return 1.099f * std::pow(v, 0.45f) - 0.099f;
}

// =============================================================================
//  Dispatcher — select transform pair by ColorSpaceMode
// =============================================================================

// Convert from the working color space to scene linear.
// Call this before applying the tone curve.
inline float toLinear(float v, ColorSpaceMode mode)
{
    switch (mode)
    {
        case ColorSpaceMode::ACEScct:          return acesCCT_to_linear(v);
        case ColorSpaceMode::DaVinciWideGamut: return dwg_to_linear(v);
        case ColorSpaceMode::Rec709:           return rec709_to_linear(v);
    }
    return acesCCT_to_linear(v); // fallback
}

// Convert from scene linear back to the working color space.
// Call this after applying the tone curve.
inline float fromLinear(float v, ColorSpaceMode mode)
{
    switch (mode)
    {
        case ColorSpaceMode::ACEScct:          return linear_to_acesCCT(v);
        case ColorSpaceMode::DaVinciWideGamut: return linear_to_dwg(v);
        case ColorSpaceMode::Rec709:           return linear_to_rec709(v);
    }
    return linear_to_acesCCT(v); // fallback
}

} // namespace CST
} // namespace MasterFilm
