// shaders/glsl/grain.glsl
// ═══════════════════════════════════════════════════════════════════════════════
// MasterFilm — Stochastic Film Grain Synthesis Kernel
// ═══════════════════════════════════════════════════════════════════════════════
//
// Boolean grain model with:
//   • Parameterised morphology (T-Grain / Cubic crystal)
//   • Auto-Regressive spatial correlation (grain clumping)
//   • Spectral dye layer crosstalk (3×3 matrix)
//   • LUT-driven tonal distribution (DLT simulation)
//   • Per-channel chroma micro-contrast decorrelation
//
// WORKSPACE
//   All inputs and outputs are 32-bit float, linearised scene-referred.
//   Grain is additive in linear space (light-matter interaction model).
//
// TEMPORAL PRNG
//   renderSeed = wangHash(uGlobalSeed + uFrameIndex)
//   Deterministic per-frame — identical grain for the same (seed, frame)
//   pair across cache re-renders and distributed machines.
//
// TILING
//   Fragment shader: every fragment has full-image texture access via uSrc.
//   Neighbour noise is regenerated on-the-fly via the hash function, so
//   there are no workgroup tile boundaries and no tiling artifacts.
//   The overlap_pixels field in FilmStockProfile is reserved for a future
//   compute-shader path where workgroups would use shared local memory
//   with halo regions.
// ═══════════════════════════════════════════════════════════════════════════════

#version 410 core

// ── Uniforms: FilmStockProfile mapped to shader interface ────────────────────

uniform sampler2D uSrc;

// Global amplitude — σ_D × 1000 from published datasheet
uniform float  uRMSGranularity;

// Morphology: 0 = Cubic (Gaussian), 1 = T-Grain (log-normal)
uniform int    uMorphologyType;

// AR spatial correlation — ring weights for neighbour convolution
uniform vec4   uARCoefficients;  // [ring1, ring1diag, ring2, ring2diag]
uniform float  uARSigma;         // centre pixel (innovation) weight

// Spectral dye layer crosstalk — row-major 3×3
// Upload with glUniformMatrix3fv(..., GL_TRUE, ...) for row-major.
uniform mat3   uSpectralMatrix;

// Chroma micro-contrast: 0.0 = monochromatic, 1.0 = full decorrelation
uniform float  uChromaMicroContrast;

// Tonal distribution LUT — 64-entry, luminance → amplitude multiplier.
// Replaces hardcoded zone weights. Sampled with linear interpolation.
#define TONAL_LUT_SIZE 64
uniform float  uTonalLUT[TONAL_LUT_SIZE];

// Deterministic temporal seed
uniform uint   uGlobalSeed;
uniform int    uFrameIndex;

// Grain size (slider) and ISO reference
uniform float  uGrainSize;
uniform float  uISO;

// ── Varyings ─────────────────────────────────────────────────────────────────

in  vec2 vTexCoord;
out vec4 fragColor;


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 1: DETERMINISTIC PRNG
// ═══════════════════════════════════════════════════════════════════════════════
//
// Wang hash — fast, combinational, no state.  Two successive calls with
// different inputs produce uncorrelated outputs.  The entire grain field
// is reproducible from (uGlobalSeed, uFrameIndex, pixelCoord, channel).

uint wangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return seed;
}

// Combine pixel position + frame seed into a single hash.
uint pixelHash(ivec2 p, uint seed)
{
    uint h = seed;
    h = wangHash(h ^ uint(p.x * 2654435761));
    h = wangHash(h ^ uint(p.y * 2246822519));
    return h;
}

// Hash → uniform float in [0, 1).
float hashToFloat(uint h)
{
    return float(h & 0xFFFFFFu) / float(0x1000000u);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 2: NOISE GENERATION
// ═══════════════════════════════════════════════════════════════════════════════

// Box-Muller transform: two uniform [0,1] → standard Gaussian.
float boxMuller(float u1, float u2)
{
    float r = sqrt(-2.0 * log(max(u1, 1e-6)));
    return r * cos(6.28318530718 * u2);
}

// Generate a Gaussian noise sample at integer pixel position p,
// for the given frame seed and channel offset.
float gaussianNoise(ivec2 p, uint frameSeed, uint channelOffset)
{
    uint h1 = pixelHash(p, frameSeed ^ channelOffset);
    uint h2 = wangHash(h1 ^ 0x6C078965u);
    return boxMuller(hashToFloat(h1), hashToFloat(h2));
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 3: MORPHOLOGY SHAPING
// ═══════════════════════════════════════════════════════════════════════════════
//
// Cubic  (morphType == 0): Gaussian distribution → identity.
// T-Grain (morphType == 1): Log-normal distribution.
//   Centred log-normal: exp(σ·Z) − exp(σ²/2), E[X]=0.
//   Produces positively skewed grain — fewer extreme outliers,
//   tighter perceived variance (Kodak T-Grain patent behaviour).

float shapeMorphology(float g)
{
    if (uMorphologyType == 1) {
        const float sigma = 0.5;
        return exp(sigma * g) - exp(0.5 * sigma * sigma);
    }
    return g;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 4: AR SPATIAL CORRELATION
// ═══════════════════════════════════════════════════════════════════════════════
//
// Symmetric FIR approximation of the AR(p) spatial agglomeration model.
// For each pixel, we regenerate Gaussian noise at its neighbours via
// the hash function (no feedback dependency — fully parallel).
//
// Neighbourhood layout (concentric rings):
//   Ring 1:      4-connected    (E, W, N, S)         weight = a[0]
//   Ring 1 diag: diagonals      (NE, NW, SE, SW)     weight = a[1]
//   Ring 2:      2-pixel axis   (2E, 2W, 2N, 2S)     weight = a[2]
//   Ring 2 diag: 2-pixel diag   (2NE, 2NW, 2SE, 2SW) weight = a[3]
//
// The weighted sum is normalised to preserve unit variance.

float arFilteredNoise(ivec2 px, uint frameSeed, uint chOffset)
{
    // Centre pixel (innovation term)
    float center = gaussianNoise(px, frameSeed, chOffset);
    center = shapeMorphology(center);
    float result = center * uARSigma;

    // Ring 1: 4-connected
    if (abs(uARCoefficients.x) > 1e-6) {
        float sum = 0.0;
        sum += shapeMorphology(gaussianNoise(px + ivec2( 1, 0), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-1, 0), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 0, 1), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 0,-1), frameSeed, chOffset));
        result += uARCoefficients.x * (sum * 0.25);
    }

    // Ring 1 diagonals
    if (abs(uARCoefficients.y) > 1e-6) {
        float sum = 0.0;
        sum += shapeMorphology(gaussianNoise(px + ivec2( 1, 1), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-1, 1), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 1,-1), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-1,-1), frameSeed, chOffset));
        result += uARCoefficients.y * (sum * 0.25);
    }

    // Ring 2: 2-pixel axis
    if (abs(uARCoefficients.z) > 1e-6) {
        float sum = 0.0;
        sum += shapeMorphology(gaussianNoise(px + ivec2( 2, 0), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-2, 0), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 0, 2), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 0,-2), frameSeed, chOffset));
        result += uARCoefficients.z * (sum * 0.25);
    }

    // Ring 2 diagonals
    if (abs(uARCoefficients.w) > 1e-6) {
        float sum = 0.0;
        sum += shapeMorphology(gaussianNoise(px + ivec2( 2, 2), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-2, 2), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2( 2,-2), frameSeed, chOffset));
        sum += shapeMorphology(gaussianNoise(px + ivec2(-2,-2), frameSeed, chOffset));
        result += uARCoefficients.w * (sum * 0.25);
    }

    // Normalise to preserve unit variance after weighted sum.
    float normSq = uARSigma * uARSigma
                 + uARCoefficients.x * uARCoefficients.x
                 + uARCoefficients.y * uARCoefficients.y
                 + uARCoefficients.z * uARCoefficients.z
                 + uARCoefficients.w * uARCoefficients.w;
    float norm = (normSq > 1e-6) ? inversesqrt(normSq) : 1.0;

    return result * norm;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 5: TONAL LUT SAMPLING
// ═══════════════════════════════════════════════════════════════════════════════
//
// Linearly interpolates the 64-entry tonal_lut at the given luminance.
// Replaces hardcoded Gaussian zone weighting — allows simulation of
// DLT shadow-smoothing, reversal shadow noise, or any measured curve.

float sampleTonalLUT(float luma)
{
    float t = clamp(luma, 0.0, 1.0) * float(TONAL_LUT_SIZE - 1);
    int i0 = int(floor(t));
    int i1 = min(i0 + 1, TONAL_LUT_SIZE - 1);
    float frac = t - float(i0);
    return mix(uTonalLUT[i0], uTonalLUT[i1], frac);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 6: SIZE-TO-SIGMA MAPPING
// ═══════════════════════════════════════════════════════════════════════════════

float sizeToSigma()
{
    // σ_base ∝ sqrt(ISO/100), scaled by grain_size slider.
    float sigmaBase = 0.35 * sqrt(uISO / 100.0);
    float scale = 0.3 + uGrainSize * 2.7;
    return sigmaBase * scale;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SECTION 7: MAIN KERNEL
// ═══════════════════════════════════════════════════════════════════════════════

void main()
{
    vec4 src = texture(uSrc, vTexCoord);

    // ── Luminance (Rec.709 coefficients in scene-linear) ─────────────────
    float luma = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));

    // ── Deterministic per-frame seed ─────────────────────────────────────
    // Seed = Hash(Global_Seed + Frame_Index)
    uint frameSeed = wangHash(uGlobalSeed + uint(uFrameIndex));

    // ── Screen-space integer pixel coordinate for hashing ────────────────
    ivec2 px = ivec2(vTexCoord * vec2(textureSize(uSrc, 0)));

    // ── Grain size → sigma scale factor ──────────────────────────────────
    float sigma = sizeToSigma();
    float amplitudeBase = (uRMSGranularity / 1000.0) * sigma * 0.04;

    // ── Per-channel hash offsets (decorrelation constants) ────────────────
    const uint chOff[3] = uint[3](0u, 0xA341316Cu, 0x62D86197u);

    // ── Generate monochromatic (shared) noise with AR correlation ────────
    float monoNoise = arFilteredNoise(px, frameSeed, 0u);

    // ── Generate per-channel noise with AR correlation ───────────────────
    vec3 rawNoise;
    rawNoise.r = arFilteredNoise(px, frameSeed, chOff[0]);
    rawNoise.g = arFilteredNoise(px, frameSeed, chOff[1]);
    rawNoise.b = arFilteredNoise(px, frameSeed, chOff[2]);

    // ── Chroma micro-contrast blend ──────────────────────────────────────
    // 0.0 = all channels receive identical (mono) noise
    // 1.0 = fully independent per-channel noise
    vec3 blendedNoise = mix(vec3(monoNoise), rawNoise, uChromaMicroContrast);

    // ── Spectral matrix application (dye layer crosstalk) ────────────────
    // uSpectralMatrix is uploaded row-major (GL_TRUE transpose flag),
    // so standard matrix × vector gives the correct result.
    vec3 grainRGB = uSpectralMatrix * blendedNoise;

    // ── Tonal LUT amplitude modulation ───────────────────────────────────
    float tonalWeight = sampleTonalLUT(luma);

    // ── Final grain amplitude ────────────────────────────────────────────
    grainRGB *= amplitudeBase * tonalWeight;

    // ── Additive composite in linear-light space ─────────────────────────
    vec3 result = src.rgb + grainRGB;

    // ── Alpha passthrough ────────────────────────────────────────────────
    fragColor = vec4(result, src.a);
}
