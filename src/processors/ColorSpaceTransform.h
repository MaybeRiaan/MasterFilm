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
        inline float acesCCT_to_linear(float v)
        {
            constexpr float CUT2 = 0.155251141552511f;  // encoded cut point (linear segment below)
            constexpr float A = 10.5402377416672f;
            constexpr float B = 0.0729055341958355f;  // encoded black floor
            constexpr float C = 0.0570776999f;        // log segment scale

            float result;
            if (v <= CUT2)
            {
                // Linear segment: very deep shadows
                result = (v - B) / A;
            }
            else
            {
                // Log segment: (2^((v - 0.413088) / 0.057077) - 2^-15) / 17.52
                result = (std::pow(2.0f, (v - 0.413088f) / C) - std::pow(2.0f, -15.0f)) / 17.52f;
            }

            // Clamp sub-black to zero — below-floor inputs have no valid linear
            // representation and would otherwise cause a hard clip artefact.
            return std::max(result, 0.0f);
        }

        // Scene linear → ACEScct encoded value
        inline float linear_to_acesCCT(float v)
        {
            constexpr float CUT1 = 0.0078125f;           // linear cut point (linear segment below)
            constexpr float A = 10.5402377416672f;
            constexpr float B = 0.0729055341958355f;
            constexpr float C = 0.0570776999f;

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