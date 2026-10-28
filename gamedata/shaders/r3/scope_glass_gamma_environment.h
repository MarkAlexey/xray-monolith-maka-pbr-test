// Diffuse environment light as the stock hmodel computes it, from the env_s0/env_s1
// cubes, env_color and L_ambient that the deferred combine binds.
#ifndef SCOPE_GLASS_GAMMA_ENVIRONMENT_H
#define SCOPE_GLASS_GAMMA_ENVIRONMENT_H
float3 scope_glass_gamma_cube_direction(float3 direction)
{
 direction /= max(max(abs(direction.x),abs(direction.y)),abs(direction.z));
 if(direction.y < .999) direction.y=direction.y*2-1;
 return normalize(direction);
}
float3 scope_glass_gamma_irradiance(float3 N,float hemi)
{
 float3 nr=scope_glass_gamma_cube_direction(N);
 float3 n2=N*N;
 // hmodel samples each axis face near its centre in the blurred mip.
 const float e=.001,diffuseMip=6;
 float3 d0=n2.x*env_s0.SampleLevel(smp_base,float3(nr.x,e,e),diffuseMip).rgb
  +n2.y*env_s0.SampleLevel(smp_base,float3(e,nr.y,e),diffuseMip).rgb
  +n2.z*env_s0.SampleLevel(smp_base,float3(e,e,nr.z),diffuseMip).rgb;
 float3 d1=n2.x*env_s1.SampleLevel(smp_base,float3(nr.x,e,e),diffuseMip).rgb
  +n2.y*env_s1.SampleLevel(smp_base,float3(e,nr.y,e),diffuseMip).rgb
  +n2.z*env_s1.SampleLevel(smp_base,float3(e,e,nr.z),diffuseMip).rgb;
 // L_ambient is not sky light, so hemi visibility does not scale it.
 return SRGBToLinear(lerp(d0,d1,env_color.w)*env_color.rgb*hemi+L_ambient.rgb);
}
#endif
