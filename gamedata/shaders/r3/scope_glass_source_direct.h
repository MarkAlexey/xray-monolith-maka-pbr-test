// Direct lighting of the lens glass: linear radiance per unit of incident light. The caller applies
// shadowing, attenuation and light colour.
#ifndef SCOPE_GLASS_SOURCE_DIRECT_H
#define SCOPE_GLASS_SOURCE_DIRECT_H
#include "scope_glass_ar_coating.h"
#include "scope_reference_brdf.h"
// The specular lobe keeps its energy and takes the hue of the coating. No diffuse term.
float3 scope_glass_coated_direct(float3 baseColor,float metallic,float specular,float roughness,float3 N,float3 V,float3 L)
{
 float nl=saturate(dot(N,L)),nv=saturate(dot(N,V));
 if(nl<=0 || nv<=0) return 0;
 float3 H=normalize(L+V);
 float nh=saturate(dot(N,H)),vh=saturate(dot(V,H));
 float3 f0=lerp(.08*specular,baseColor,metallic);
 float f90=lerp(2*specular,1,metallic);
 float3 reflected=scope_reference_ggx_specular(f0,f90,clamp(roughness,scope_reference_min_roughness,1),nl,nv,nh,vh);
 return scope_glass_coating_hue(reflected,vh)*nl;
}
// Dirt raises roughness and lowers metallic together; recover metallic from roughness.
// source17: x dirt limit, y metallic lost to full dirt, z clean metallic.
// source18: x dirt exponent, y roughness span, z clean roughness.
float scope_glass_source_metallic(float rawRoughness,float4 source17,float4 source18)
{
 float roughness=saturate(rawRoughness),dirt=0;
 if(source18.x>1.e-5 && source18.y>1.e-5)
  dirt=pow(saturate((roughness-source18.z)/source18.y),1/source18.x);
 dirt=min(dirt,max(source17.x,0));
 return saturate(source17.z-dirt*source17.y);
}
#endif
