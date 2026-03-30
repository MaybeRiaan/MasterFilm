// shaders/glsl/acutance.glsl
// Pass 5: Edge emphasis — three character modes.
// Soft:     gentle unsharp mask, low frequency emphasis only
// Natural:  standard unsharp mask
// Enhanced: unsharp mask + Laplacian adjacency (Kostinsky) term for MTF > 1.0

#version 410 core

uniform sampler2D uSrc;
uniform int   uCharacter;         // 0=Soft, 1=Natural, 2=Enhanced
uniform float uIntensity;         // Edge strength [0,1]
uniform float uRolloff;           // Frequency rolloff [0,1] → blur sigma
uniform float uKostinskyStrength; // Adjacency term weight (Enhanced only)

in  vec2 vTexCoord;
out vec4 fragColor;

// ── Box blur (fast approximation for the blurred layer) ───────────────────────

vec3 boxBlur(float sigma)
{
    ivec2 texSize = textureSize(uSrc, 0);
    vec2  texel   = 1.0 / vec2(texSize);
    int   r       = int(ceil(sigma));

    vec3  accum = vec3(0.0);
    float count = 0.0;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            accum += texture(uSrc, vTexCoord + vec2(x, y) * texel).rgb;
            count += 1.0;
        }
    }
    return accum / count;
}

// ── Laplacian (adjacency / Kostinsky term) ────────────────────────────────────

vec3 laplacian()
{
    ivec2 texSize = textureSize(uSrc, 0);
    vec2  t       = 1.0 / vec2(texSize);

    vec3 c  = texture(uSrc, vTexCoord).rgb;
    vec3 n  = texture(uSrc, vTexCoord + vec2( 0,  1) * t).rgb;
    vec3 s  = texture(uSrc, vTexCoord + vec2( 0, -1) * t).rgb;
    vec3 e  = texture(uSrc, vTexCoord + vec2( 1,  0) * t).rgb;
    vec3 w  = texture(uSrc, vTexCoord + vec2(-1,  0) * t).rgb;

    // 4-connected discrete Laplacian
    return (n + s + e + w) - 4.0 * c;
}

void main()
{
    vec3 src = texture(uSrc, vTexCoord).rgb;
    float alpha = texture(uSrc, vTexCoord).a;

    // Blur sigma: rolloff [0,1] → [0.5, 4.0] texels
    float sigma = 0.5 + uRolloff * 3.5;

    // Soft mode uses larger sigma (emphasises only coarser edges)
    if (uCharacter == 0) sigma *= 2.0;

    vec3 blurred = boxBlur(sigma);
    vec3 highFreq = src - blurred;  // Unsharp residual

    // Intensity scaling: Enhanced is boosted above neutral
    float boost = (uCharacter == 2) ? 1.35 : 1.0;
    vec3 result = src + highFreq * uIntensity * boost;

    // Kostinsky adjacency term (Enhanced only)
    if (uCharacter == 2) {
        vec3 lap = laplacian();
        // Adjacency: positive response at dark→bright transitions (subtractive at bright edges)
        // This causes MTF to rise above 1.0 at mid-spatial-frequencies — matches Velvia behavior
        result -= lap * uKostinskyStrength * uIntensity;
    }

    fragColor = vec4(result, alpha);
}
