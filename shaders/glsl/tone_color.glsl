// shaders/glsl/tone_color.glsl
// Full 5-stage photochemical film emulation pipeline (GPU).
//
// Matches UnifiedFilmProcessor::processCPU exactly:
//   Stage 1 - Exposure:  encoded -> scene linear -> log2 stops
//   Stage 2 - Negative:  per-channel H&D density LUT + filmColor blend + coupling
//   Stage 3 - Timing:    printer light density offsets
//   Stage 4 - Print:     density delta -> print LUT -> encoded output
//   Stage 5 - Zone:      HSL hue shift + saturation (artistic post-process)

#version 410 core

// ---- Textures ---------------------------------------------------------------
uniform sampler2D uSrc;        // source image (TEXTURE0)
uniform sampler2D uNegLUT;     // negative density LUTs, R/G/B in channels (TEXTURE2)
                               // 4096x1 RGBA32F, domain: log2 stops [-8, 9]
uniform sampler2D uPrintLUT;   // print exit ramp LUTs, R/G/B in channels (TEXTURE3)
                               // 4096x1 RGBA32F, domain: density delta [-3, 3]

// ---- Color space mode -------------------------------------------------------
uniform int uColorSpaceMode;   // 0 = ACEScct, 1 = DaVinci Intermediate

// ---- Negative LUT domain ----------------------------------------------------
uniform float uStopsMin;       // -8.0
uniform float uStopsRange;     // 17.0
uniform float uLUTSize;        // 4096.0

// ---- Print LUT domain -------------------------------------------------------
uniform float uDeltaMin;       // -3.0
uniform float uDeltaRange;     // 6.0
uniform float uPrintLUTSize;   // 4096.0

// ---- Film parameters --------------------------------------------------------
uniform float uFilmColor;      // 0 = mono (R/B collapse to G), 1 = full stock colour
uniform vec3  uExitDMid;       // exit anchor (precomputed on CPU for filmColor+coupling)
uniform vec3  uTimingOffset;   // printer light offsets in stop units (0 = neutral)
uniform float uLog2of10;       // 3.321928

// ---- Coupling matrix --------------------------------------------------------
uniform mat3  uCouplingMatrix;
uniform int   uCouplingIsIdentity;  // 0 or 1

// ---- Enable flags -----------------------------------------------------------
uniform int   uEnableTone;     // 0 = skip tone (stages 1-4), 1 = apply
uniform int   uEnableColor;    // 0 = skip zone color (stage 5), 1 = apply

// ---- Zone color parameters --------------------------------------------------
uniform float uHueShadow;
uniform float uHueMid;
uniform float uHueHighlight;
uniform float uSatShadow;
uniform float uSatMid;
uniform float uSatHighlight;
uniform vec3  uLumaCoeffs;     // luma weights for current colour space

in  vec2 vTexCoord;
out vec4 fragColor;

// =============================================================================
//  Color space: encoded -> scene linear
// =============================================================================

float toLinear(float v)
{
    if (uColorSpaceMode == 0)
    {
        // ACEScct (Academy S-2014-003)
        const float CUT = 0.155251141552511;
        const float A   = 10.5402377416672;
        const float B   = 0.0729055341958355;
        if (v <= CUT)
            return max((v - B) / A, 0.0);
        else
            return pow(2.0, v * 17.52 - 9.72);
    }
    else
    {
        // DaVinci Intermediate (BMD 2021)
        const float DI_A       = 0.0075;
        const float DI_B       = 7.0;
        const float DI_C       = 0.07329248;
        const float DI_M       = 10.44426855;
        const float DI_LOG_CUT = 0.02740668;
        if (v <= DI_LOG_CUT)
            return max(v / DI_M, 0.0);
        else
            return max(pow(2.0, v / DI_C - DI_B) - DI_A, 0.0);
    }
}

// =============================================================================
//  LUT sampling with half-texel-correct addressing
// =============================================================================

// Map a value in [rangeMin, rangeMin+rangeSpan] to a texture coordinate
// that accounts for GL_LINEAR half-texel-centre convention.
float lutTexCoord(float value, float rangeMin, float rangeSpan, float lutSize)
{
    float norm = clamp((value - rangeMin) / rangeSpan, 0.0, 1.0);
    // Map [0,1] -> [0.5/size, 1-0.5/size] for texel-centre alignment
    return (norm * (lutSize - 1.0) + 0.5) / lutSize;
}

// =============================================================================
//  HSL helpers (zone colour)
// =============================================================================

vec3 rgbToHsl(vec3 c)
{
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float l    = (maxC + minC) * 0.5;
    float delta = maxC - minC;

    if (delta < 1e-6) return vec3(0.0, 0.0, l);

    float s = delta / (1.0 - abs(2.0 * l - 1.0));
    float h;
    if      (maxC == c.r) h = mod((c.g - c.b) / delta, 6.0);
    else if (maxC == c.g) h = (c.b - c.r) / delta + 2.0;
    else                  h = (c.r - c.g) / delta + 4.0;
    h *= 60.0;
    if (h < 0.0) h += 360.0;

    return vec3(h, s, l);
}

vec3 hslToRgb(vec3 hsl)
{
    float h = hsl.x, s = hsl.y, l = hsl.z;
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - abs(mod(h / 60.0, 2.0) - 1.0));
    float m = l - c * 0.5;

    vec3 rgb;
    int sect = int(h / 60.0) % 6;
    if      (sect == 0) rgb = vec3(c, x, 0);
    else if (sect == 1) rgb = vec3(x, c, 0);
    else if (sect == 2) rgb = vec3(0, c, x);
    else if (sect == 3) rgb = vec3(0, x, c);
    else if (sect == 4) rgb = vec3(x, 0, c);
    else                rgb = vec3(c, 0, x);

    return rgb + vec3(m);
}

// Zone blend weights
float shadowW(float luma)    { float t = max(0.0, 1.0 - luma / 0.35); return t * t; }
float midW(float luma)       { float d = (luma - 0.5) / 0.3; return exp(-0.5 * d * d); }
float highlightW(float luma) { float t = max(0.0, (luma - 0.65) / 0.35); return t * t; }

// =============================================================================
//  Main
// =============================================================================

void main()
{
    vec4 src = texture(uSrc, vTexCoord);
    vec3 rgb = src.rgb;

    // ---- Stages 1-4: Tone curve (photochemical pipeline) --------------------
    if (uEnableTone != 0)
    {
        // Stage 1: Exposure -- decode to linear, then to log2 stops
        const float kFloor      = 1e-10;
        const float kMiddleGrey = 0.18;

        vec3 lin = vec3(toLinear(rgb.r), toLinear(rgb.g), toLinear(rgb.b));
        vec3 stops = vec3(
            log2(max(lin.r, kFloor) / kMiddleGrey),
            log2(max(lin.g, kFloor) / kMiddleGrey),
            log2(max(lin.b, kFloor) / kMiddleGrey)
        );

        // Stage 2: Negative -- sample per-channel density LUTs
        // Each sample returns vec4(negR, negG, negB, 0) at that stops value.
        float tcR = lutTexCoord(stops.r, uStopsMin, uStopsRange, uLUTSize);
        float tcG = lutTexCoord(stops.g, uStopsMin, uStopsRange, uLUTSize);
        float tcB = lutTexCoord(stops.b, uStopsMin, uStopsRange, uLUTSize);

        vec3 negAtR = texture(uNegLUT, vec2(tcR, 0.5)).rgb;
        vec3 negAtG = texture(uNegLUT, vec2(tcG, 0.5)).rgb;
        vec3 negAtB = texture(uNegLUT, vec2(tcB, 0.5)).rgb;

        // Per-channel density: R curve at R stops, G curve at G stops, B curve at B stops
        vec3 density = vec3(negAtR.r, negAtG.g, negAtB.b);

        // Film color blend -- lerp R/B density toward green curve at same exposure
        if (uFilmColor < 1.0)
        {
            float gForR = negAtR.g;  // green density curve sampled at red's stops
            float gForB = negAtB.g;  // green density curve sampled at blue's stops
            density.r = mix(gForR, density.r, uFilmColor);
            density.b = mix(gForB, density.b, uFilmColor);
        }

        // Inter-layer coupling (density-space matrix)
        if (uCouplingIsIdentity == 0)
            density = uCouplingMatrix * density;

        // Stage 3: Timing -- printer light offsets
        // timingOffset is in stop units; convert to density via /log2(10)
        density -= uTimingOffset / uLog2of10;

        // Stage 4: Print -- density delta -> exit ramp LUT -> encoded output
        vec3 delta = density - uExitDMid;

        float ptcR = lutTexCoord(delta.r, uDeltaMin, uDeltaRange, uPrintLUTSize);
        float ptcG = lutTexCoord(delta.g, uDeltaMin, uDeltaRange, uPrintLUTSize);
        float ptcB = lutTexCoord(delta.b, uDeltaMin, uDeltaRange, uPrintLUTSize);

        vec3 printAtR = texture(uPrintLUT, vec2(ptcR, 0.5)).rgb;
        vec3 printAtG = texture(uPrintLUT, vec2(ptcG, 0.5)).rgb;
        vec3 printAtB = texture(uPrintLUT, vec2(ptcB, 0.5)).rgb;

        rgb = vec3(printAtR.r, printAtG.g, printAtB.b);
    }

    // ---- Stage 5: Zone color (artistic post-process) ------------------------
    if (uEnableColor != 0)
    {
        float luma = dot(rgb, uLumaCoeffs);
        float ws = shadowW(luma);
        float wm = midW(luma);
        float wh = highlightW(luma);

        float hueShift = ws * uHueShadow + wm * uHueMid + wh * uHueHighlight;
        float satScale = 1.0
                       + ws * (uSatShadow    - 1.0)
                       + wm * (uSatMid       - 1.0)
                       + wh * (uSatHighlight - 1.0);

        vec3 hsl  = rgbToHsl(rgb);
        hsl.x     = mod(hsl.x + hueShift + 360.0, 360.0);
        hsl.y     = clamp(hsl.y * satScale, 0.0, 1.0);
        rgb       = hslToRgb(hsl);
    }

    fragColor = vec4(rgb, src.a);
}
