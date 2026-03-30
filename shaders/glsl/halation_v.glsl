// shaders/glsl/halation_v.glsl
// Pass 3: Vertical separable Gaussian + composite halation onto source.

#version 410 core

uniform sampler2D uSrc;        // Original (pre-halation) image
uniform sampler2D uHBlur;      // Horizontal blur output from Pass 2
uniform float uInnerRadius;
uniform float uOuterRadiusScale;
uniform float uOuterWeight;
uniform float uThreshold;
uniform vec3  uSpecBias;
uniform float uIntensity;      // Overall halation strength [0,1]

in  vec2 vTexCoord;
out vec4 fragColor;

float gauss(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma));
}

vec3 blurV(sampler2D tex, float sigma)
{
    if (sigma < 0.5) return texture(tex, vTexCoord).rgb;

    ivec2 texSize = textureSize(tex, 0);
    float texelH  = 1.0 / float(texSize.y);

    int   radius = int(ceil(sigma * 3.0));
    vec3  accum  = vec3(0.0);
    float weight = 0.0;

    for (int i = -radius; i <= radius; ++i) {
        vec2  offset = vec2(0.0, float(i) * texelH);
        vec3  s      = texture(tex, vTexCoord + offset).rgb;
        float w      = gauss(float(i), sigma);
        accum  += s * w;
        weight += w;
    }
    return (weight > 0.0) ? accum / weight : vec3(0.0);
}

void main()
{
    vec4 src = texture(uSrc, vTexCoord);

    // Vertical blur of the horizontal-blurred halation layer
    vec3 innerV = blurV(uHBlur, uInnerRadius);
    vec3 outerV = blurV(uHBlur, uInnerRadius * uOuterRadiusScale);
    vec3 halationLayer = mix(innerV, outerV, uOuterWeight);

    // Additive composite — halation only brightens (physically accurate)
    vec3 result = src.rgb + halationLayer * uIntensity;

    fragColor = vec4(result, src.a);
}
