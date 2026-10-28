#ifndef SCOPE_GLASS_LIVE_ENVIRONMENT_H
#define SCOPE_GLASS_LIVE_ENVIRONMENT_H
#include "svp_lens_environment.h"
float4 svp_lens_environment;
float4 scope_glass_probe_positive,scope_glass_probe_negative;
float scope_glass_live_probe_visibility(float3 direction)
{
 float3 positive=max(direction,0),negative=max(-direction,0);
 float3 probePositive=max(scope_glass_probe_positive.xyz,0);
 float3 probeNegative=max(scope_glass_probe_negative.xyz,0);
 float mean=dot(probePositive+probeNegative,1)/6;
 float weight=dot(positive+negative,1);
 // Visibility relative to the probe's mean, so its overall brightness cancels.
 return saturate((dot(positive,probePositive)+dot(negative,probeNegative))/max(mean*weight,.0001));
}
// Mean luminance of the frozen clear-noon cube this path used as its structure mask, kept as
// the exposure so the lens level matches; the active weather cubes now supply the structure.
static const float scope_glass_live_structure=.555;
// Cube mip for a GGX lobe: alpha=roughness^2 is the median microfacet slope, reflection doubles
// it, so the lobe covers about 2*width*alpha texels of a face (box-filtered mips).
float scope_glass_lobe_lod(uint width,uint levels,float roughness)
{
 float r=saturate(roughness);
 return clamp(log2(max(2.0*max(float(width),1.0)*r*r,1.0)),0.0,max(float(levels)-1.0,0.0));
}
float scope_glass_reflection_lod(float roughness)
{
 uint width,height,levels;
 s_lens_env0.GetDimensions(0,width,height,levels);
 return scope_glass_lobe_lod(width,levels,roughness);
}
// The active weather cubes in their own convention, times the weather colour (the env colour
// signal, which carries day and night); the probe only shapes the direction.
float3 scope_glass_live_environment_radiance(float3 direction,float roughness)
{
 direction=normalize(direction);
 float3 weather=svp_lens_weather(direction,scope_glass_reflection_lod(roughness),svp_lens_environment.w)
  *max(svp_lens_environment.rgb,0);
 // The weather cubes are sRGB-encoded; visibility scales the decoded light.
 return SRGBToLinear(weather)*scope_glass_live_structure*scope_glass_live_probe_visibility(direction);
}
#endif
