// shaders/glsl/halation_h.glsl
// Pass 2: Horizontal separable Gaussian blur for halation.
// Two-lobe model: inner (tight glow) + outer (wide spread).
// Only bright pixels above uThreshold contribute — rest pass through.

#version 410 core

uniform sampler2D uSrc;
uniform float uInnerRadius;      // Inner Gaussian σ in texels
uniform float uOuterRadiusScale; // Outer σ = uInnerRadius * uOuterRadiusScale
uniform float uOuterWeight;      // Relative weight of outer lobe [0,1]
uniform float uThreshold;        // Luminance threshold [0,1]
uniform vec3  uSpecBias;         // Per-channel spectral bias (R, G, B)

in  vec2 vTexCoord;
out vec4 fragColor;

// ── Gaussian weight ───────────────────────────────────────────────────────────

float gauss(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma));
}

// ── Horizontal blur ───────────────────────────────────────────────────────────

vec3 blurH(float sigma)
{
    if (sigma < 0.5) return texture(uSrc, vTexCoord).rgb;

    ivec2 texSize = textureSize(uSrc, 0);
    float texelW  = 1.0 / float(texSize.x);

    int radius = int(ceil(sigma * 3.0));
    vec3  accum  = vec3(0.0);
    float weight = 0.0;

    for (int i = -radius; i <= radius; ++i) {
        vec2 offset = vec2(float(i) * texelW, 0.0);
        vec4 sample = texture(uSrc, vTexCoord + offset);

        float luma = dot(sample.rgb, vec3(0.2126, 0.7152, 0.0722));
        // Only pixels above threshold contribute to the halation layer
        float contrib = max(0.0, luma - uThreshold) / max(1.0 - uThreshold, 0.001);

        float w = gauss(float(i), sigma);
        // Apply spectral bias: brighter contribution on red channel, etc.
        accum  += sample.rgb * uSpecBias * contrib * w;
        weight += w;
    }

    return (weight > 0.0) ? accum / weight : vec3(0.0);
}

void main()
{
    vec4 src = texture(uSrc, vTexCoord);

    // Inner lobe
    vec3 innerBlur = blurH(uInnerRadius);
    // Outer lobe (wider, dimmer)
    vec3 outerBlur = blurH(uInnerRadius * uOuterRadiusScale);

    // Combined halation layer (stored for vertical pass)
    vec3 halation = mix(innerBlur, outerBlur, uOuterWeight);

    fragColor = vec4(halation, src.a);
}
