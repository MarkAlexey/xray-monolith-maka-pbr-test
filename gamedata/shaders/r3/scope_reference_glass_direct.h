// Direct lighting of the forward glass material: three.js isotropic GGX with
// multiple scattering and Lambert. Light range, cone and visibility come from the engine.
#ifndef SCOPE_REFERENCE_GLASS_DIRECT_H
#define SCOPE_REFERENCE_GLASS_DIRECT_H
#include "scope_reference_glass_lighting.h"
#include "scope_reference_glass_lights.h"
float3 scope_reference_glass_direct_brdf(ScopeReferenceGlass material,float3 N,float3 V,
 float3 L,float3 incident,float geometryRoughness)
{
 float nl=saturate(dot(N,L)),nv=saturate(dot(N,V));
 if(nl<=0 || nv<=0)return 0;
 float3 H=normalize(L+V);
 float nh=saturate(dot(N,H)),vh=saturate(dot(V,H));
 float roughness=scope_reference_glass_roughness(material,geometryRoughness);
 float3 f0=lerp((.08*material.specular).xxx,material.baseColor,material.metallic);
 float f90=lerp(2*material.specular,1,material.metallic);
 float3 diffuse=material.baseColor*(1-material.metallic)/3.14159265359;
 return max(incident,0)*nl*(scope_reference_ggx_specular(f0,f90,roughness,nl,nv,nh,vh)+diffuse);
}
float3 scope_reference_glass_local(ScopeReferenceGlass material,float3 P,float3 N,float3 V,
 float geometryRoughness,float4 position,float4 color,float4 direction)
{
 float3 delta=position.xyz-P;float d2=dot(delta,delta);
 if(position.w<=0 || color.w<=0 || d2<=1.e-12)return 0;
 float3 L=delta*rsqrt(d2);
 return scope_reference_glass_direct_brdf(material,N,V,L,
  scope_reference_light_incident(color,direction,position.w,d2,L),geometryRoughness);
}
float3 scope_reference_glass_direct(ScopeReferenceGlass material,float3 P,float3 N,float3 V,float geometryRoughness)
{
 float3 result=scope_reference_glass_local(material,P,N,V,geometryRoughness,
  scope_glass_light0_position,scope_glass_light0_color,scope_glass_light0_direction);
 result+=scope_reference_glass_local(material,P,N,V,geometryRoughness,
  scope_glass_light1_position,scope_glass_light1_color,scope_glass_light1_direction);
 float sunLength2=dot(L_sun_dir_w,L_sun_dir_w);
 if(sunLength2>1.e-12)result+=scope_reference_glass_direct_brdf(material,N,V,
  -L_sun_dir_w*rsqrt(sunLength2),L_sun_color.rgb*svp_optic_sun_visibility(),geometryRoughness);
 return result;
}
float scope_reference_glass_environment_visibility(float3 direction)
{
 // The six-face probe gives relative direction only; hemi gives the absolute level.
 return svp_optic_probe_visibility(direction)*svp_optic_hemi_visibility();
}
#endif
