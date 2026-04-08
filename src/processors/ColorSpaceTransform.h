// src/processors/ColorSpaceTransform.h
// Analytical color space transfer functions for MasterFilm's internal CST.
//
// All functions convert to/from scene linear light, normalised so that
// 0.18 = 18% grey (middle grey). Values above 1.0 are valid and expected —
// specular highlights and light sources routinely exceed 1.0 in scene linear.
//
// Supported spaces:
//   ACEScct  — ACES specification S-2014-003 (Academy)
//   DaVinci Intermediate — Blackmagic Design DaVinci Intermediate white paper (2021)
//
// Rec.709 is intentionally excluded — it is display-referred with no
// highlight headroom above 1.0, which is incompatible with the H&D curve
// model. See ColorSpaceMode in FilmPreset.h for the full rationale.
//
// All functions are pure inline — no state, no heap, no dependencies.
// Safe to call from any thread.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cmath>
#include <algorithm>
#include "../presets/FilmPreset.h"   // ColorSpaceMode

namespace MasterFilm {
    namespace CST {

        // =============================================================================
        //  ACEScct  ↔  Scene Linear (AP1 primaries)
        //  Source: Academy S-2014-003
        // =============================================================================

        // ACEScct encoded value → scene linear
        // Output is clamped to >= 0. Encoded values below the ACEScct black floor
        // (0.0729) produce negative linear values from the piecewise formula —
        // these are sub-black and have no physical meaning in this context.
        // ACEScct encoded value → scene linear
        // Source: Academy S-2014-003 (verified against OCIO source)
        inline float acesCCT_to_linear(float v)
        {
            // cut point: (log2(0.0078125) + 9.72) / 17.52 = 0.155251...
            constexpr float CUT_CCT = 0.155251141552511f;
            constexpr float A       = 10.5402377416672f;
            constexpr float B       = 0.0729055341958355f;

            float result;
            if (v <= CUT_CCT)
                result = (v - B) / A;           // linear segment
            else
                result = std::pow(2.0f, v * 17.52f - 9.72f);   // log segment

            return std::max(result, 0.0f);
        }

        // Scene linear → ACEScct encoded value
        // Source: Academy S-2014-003
        inline float linear_to_acesCCT(float v)
        {
            constexpr float CUT_LIN = 0.0078125f;
            constexpr float A       = 10.5402377416672f;
            constexpr float B       = 0.0729055341958355f;

            if (v <= CUT_LIN)
                return A * v + B;               // linear segment
            else
                return (std::log2(std::max(v, 1e-10f)) + 9.72f) / 17.52f;  // log segment
        }

        // =============================================================================
        //  DaVinci Intermediate  ↔  Scene Linear
        //  Source: Blackmagic Design DaVinci Intermediate white paper (2021)
        //
        //  Encoding:  V = L * DI_M                          (L <= DI_LIN_CUT)
        //             V = DI_C * (log2(L + DI_A) + DI_B)   (L >  DI_LIN_CUT)
        //
        //  Decoding:  L = V / DI_M                          (V <= DI_LOG_CUT)
        //             L = 2^(V/DI_C - DI_B) - DI_A          (V >  DI_LOG_CUT)
        //
        //  Constants: DI_A = 0.0075, DI_B = 7.0, DI_C = 0.07329248
        //             DI_M = 10.44426855
        //             DI_LIN_CUT = 0.00262409, DI_LOG_CUT = 0.02740668
        // =============================================================================

        // DaVinci Intermediate encoded value → scene linear
        inline float dwg_to_linear(float v)
        {
            constexpr float DI_A = 0.0075f;
            constexpr float DI_B = 7.0f;
            constexpr float DI_C = 0.07329248f;
            constexpr float DI_M = 10.44426855f;
            constexpr float DI_LOG_CUT = 0.02740668f;

            float result;
            if (v <= DI_LOG_CUT)
            {
                // Linear segment
                result = v / DI_M;
            }
            else
            {
                // Exponential segment: 2^(V/DI_C - DI_B) - DI_A
                result = std::pow(2.0f, v / DI_C - DI_B) - DI_A;
            }

            // Clamp sub-black to zero
            return std::max(result, 0.0f);
        }

        // Scene linear → DaVinci Intermediate encoded value
        inline float linear_to_dwg(float v)
        {
            constexpr float DI_A = 0.0075f;
            constexpr float DI_B = 7.0f;
            constexpr float DI_C = 0.07329248f;
            constexpr float DI_M = 10.44426855f;
            constexpr float DI_LIN_CUT = 0.00262409f;

            if (v <= DI_LIN_CUT)
            {
                // Linear segment: V = L * DI_M
                return v * DI_M;
            }
            else
            {
                // Log segment: V = DI_C * (log2(L + DI_A) + DI_B)
                return DI_C * (std::log2(v + DI_A) + DI_B);
            }
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
            }
            return linear_to_acesCCT(v); // fallback
        }

    } // namespace CST
} // namespace MasterFilm