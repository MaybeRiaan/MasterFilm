// shaders/glsl/tone_color.glsl
// Pass 1: Combined tone curve evaluation + inter-layer coupling + zone hue/sat.
// Tone curve is baked into a 1D LUT texture (1024 entries, R32F).

#version 410 core

uniform sampler2D uSrc;
uniform sampler1D uToneLUT;         // 1D LUT (1024 float), R channel only

// Color: inter-layer coupling matrix (row-major)
uniform mat3 uCouplingMatrix;

// Zone hue shifts (degrees)
uniform float uHueShadow;
uniform float uHueMid;
uniform float uHueHighlight;

// Zone saturation scales
uniform float uSatShadow;
uniform float uSatMid;
uniform float uSatHighlight;

in  vec2 vTexCoord;
out vec4 fragColor;

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// ── Main ──────────────────────────────────────────────────────────────────────

void main()
{
    vec4 src = texture(uSrc, vTexCoord);
    vec3 rgb = src.rgb;

    // 1. Inter-layer coupling
    rgb = uCouplingMatrix * rgb;

    // 2. Tone curve via 1D LUT
    rgb.r = texture(uToneLUT, rgb.r).r;
    rgb.g = texture(uToneLUT, rgb.g).r;
    rgb.b = texture(uToneLUT, rgb.b).r;

    // 3. Zone hue + saturation
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float ws = shadowW(luma);
    float wm = midW(luma);
    float wh = highlightW(luma);

    float hueShift  = ws * uHueShadow + wm * uHueMid + wh * uHueHighlight;
    float satScale  = 1.0
                    + ws * (uSatShadow    - 1.0)
                    + wm * (uSatMid       - 1.0)
                    + wh * (uSatHighlight - 1.0);

    vec3 hsl  = rgbToHsl(rgb);
    hsl.x     = mod(hsl.x + hueShift + 360.0, 360.0);
    hsl.y     = clamp(hsl.y * satScale, 0.0, 1.0);
    rgb       = hslToRgb(hsl);

    fragColor = vec4(rgb, src.a);
}
