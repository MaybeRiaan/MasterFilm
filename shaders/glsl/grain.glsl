// shaders/glsl/grain.glsl
// Pass 4: Procedural grain synthesis.
// PSD-shaped noise via per-pixel hash + zone weighting.
// Uniforms injected by GrainProcessor::processGPU()

#version 410 core

uniform sampler2D uSrc;
uniform float uAmount;
uniform float uSigma;        // Grain size in texels (from sizeToSigma)
uniform float uRoughness;
uniform vec3  uZoneWeights;  // .x=shadow .y=mid .z=highlight
uniform int   uSeed;

in  vec2 vTexCoord;
out vec4 fragColor;

// ── Hash / noise ──────────────────────────────────────────────────────────────

// Hash from texel coordinate + seed — avoids tiling artifacts
float hash(vec2 p, int seed)
{
    float s = float(seed) * 127.1;
    p += vec2(s, s * 0.7183);
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

// Box-Muller transform: uniform [0,1] → Gaussian
vec2 gaussianPair(float u1, float u2)
{
    float r = sqrt(-2.0 * log(max(u1, 1e-6)));
    float theta = 6.28318530718 * u2;
    return vec2(r * cos(theta), r * sin(theta));
}

// ── Zone weight at luminance ──────────────────────────────────────────────────

float zoneWeight(float luma)
{
    float wS = uZoneWeights.x * exp(-0.5 * pow((luma - 0.15) / 0.18, 2.0));
    float wM = uZoneWeights.y * exp(-0.5 * pow((luma - 0.50) / 0.22, 2.0));
    float wH = uZoneWeights.z * exp(-0.5 * pow((luma - 0.85) / 0.18, 2.0));
    return wS + wM + wH;
}

// ── Grain synthesis ───────────────────────────────────────────────────────────

void main()
{
    vec4 src = texture(uSrc, vTexCoord);

    float luma = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));

    // Screen-space pixel coordinate for hashing
    vec2 px = vTexCoord * vec2(textureSize(uSrc, 0));

    // Coarse + fine noise layers (roughness blends between them)
    // Coarse: larger-scale clumping driven by uRoughness
    vec2 coarseCell = floor(px / max(uSigma * 1.5, 1.0));
    float u1c = hash(coarseCell, uSeed);
    float u2c = hash(coarseCell + vec2(17.3, 31.7), uSeed);
    float coarseNoise = gaussianPair(u1c, u2c).x;

    // Fine: per-pixel
    float u1f = hash(px, uSeed + 1);
    float u2f = hash(px + vec2(47.9, 83.1), uSeed + 1);
    float fineNoise = gaussianPair(u1f, u2f).x;

    float noise = mix(fineNoise, coarseNoise, uRoughness);

    // Zone weight + amount scaling
    float weight = zoneWeight(luma);
    float grainVal = noise * uAmount * weight * (uSigma * 0.04);

    // Monochromatic grain (color grain variation can be added as Phase 2)
    vec3 result = src.rgb + vec3(grainVal);

    fragColor = vec4(result, src.a);
}
