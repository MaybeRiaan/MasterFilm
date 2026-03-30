// src/processors/HalationProcessor.cpp
#include "HalationProcessor.h"
#include <cmath>

namespace MasterFilm {

float HalationProcessor::radiusInPixels(int imageHeight) const
{
    // 0.0 → 0 px, 1.0 → 3% of image height (two-Gaussian inner lobe)
    return mParams.radius * static_cast<float>(imageHeight) * 0.03f;
}

OfxStatus HalationProcessor::processHorizontalGPU(OfxImageEffectHandle,
                                                   OfxPropertySetHandle,
                                                   OfxPropertySetHandle) const
{
    // TODO: dispatch shaders/glsl/halation_h.glsl / shaders/metal/halation_h.metal
    // Uniforms: innerRadius, outerRadius (innerRadius * outerRadiusScale),
    //           threshold, biasR, biasG, biasB
    return kOfxStatReplyDefault;
}

OfxStatus HalationProcessor::processVerticalGPU(OfxImageEffectHandle,
                                                 OfxPropertySetHandle,
                                                 OfxPropertySetHandle,
                                                 OfxPropertySetHandle) const
{
    // TODO: dispatch shaders/glsl/halation_v.glsl
    // Composite: dst = src + halation_layer * intensity
    return kOfxStatReplyDefault;
}

OfxStatus HalationProcessor::processCPU(const float*, float*,
                                        int, int, int) const
{
    // TODO: separable Gaussian blur + threshold + composite CPU path
    return kOfxStatReplyDefault;
}

} // namespace MasterFilm
