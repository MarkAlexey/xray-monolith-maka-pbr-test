// Forward material for the scope's own glass mesh.
#ifndef SCOPE_REFERENCE_PHYSICAL_GLASS_H
#define SCOPE_REFERENCE_PHYSICAL_GLASS_H
#include "svp_lens_optics.h"
#define SCOPE_REFERENCE_GLASS_SAMPLER_DECLARED
#define scope_reference_glass_sampler smp_base
#include "scope_reference_glass.h"
#include "scope_reference_glass_lighting.h"
#include "scope_reference_glass_direct.h"
float4 scope_reference_physical_material(float2 uv, float3 P, float3 T, float3 B, float3 N, float3 V)
{
    N=normalize(N); V=normalize(V);
    N*=dot(N,V)<0 ? -1 : 1;
    T=normalize(T-N*dot(N,T));
    float handedness=dot(cross(N,T),B)<0 ? -1 : 1;
    B=normalize(cross(N,T))*handedness;
    ScopeReferenceGlass material=scope_reference_glass(uv,T,N,B,V);
    // The material's colour falls off towards its own rim, which would show as
    // an inset disc on this mesh. Take colour and emission from the centre and
    // keep roughness, normal and spots at the mesh UV.
    ScopeReferenceGlass coating=scope_reference_glass(float2(.5,.5),T,N,B,V);
    material.baseColor=coating.baseColor;
    material.emission=coating.emission;
    svp_lens_adjust_dirt(material.roughness,material.metallic,scope_reference_glass_cb[17],scope_reference_glass_cb[18]);
    N=normalize(material.worldNormal);
    float3 gradients=max(abs(ddx(N)),abs(ddy(N)));
    float geometryRoughness=max(gradients.x,max(gradients.y,gradients.z));
    float roughness=scope_reference_glass_roughness(material,geometryRoughness);
    float lookupRoughness=svp_lens_blur_roughness(roughness);
    float3 ray=normalize(lerp(reflect(-V,N),N,pow(lookupRoughness,4)));
    // Cube mip for a mirror reflection and for diffuse light.
    const float sharpMip=.8,diffuseMip=4.5;
    float3 radiance=svp_optic_environment(ray,lerp(sharpMip,diffuseMip,lookupRoughness))
        *svp_optic_probe_visibility(ray);
    float3 irradiance=svp_optic_environment(N,diffuseMip)
        *scope_reference_glass_environment_visibility(N);
    float3 single,diffuse;
    scope_reference_glass_coefficients(material,N,V,geometryRoughness,single,diffuse);
    SvpLensProfile profile=svp_default_lens_profile();
    float3 reflection=(svp_lens_authored_reflectance(single)*radiance+diffuse*irradiance)*profile.reflection_exposure
        *svp_lens_environment_scale();
    reflection+=scope_reference_glass_direct(material,P,N,V,geometryRoughness)*svp_lens_glint_scale()+material.emission;
    float3 loss=saturate(single+diffuse);
    float alpha=max(loss.x,max(loss.y,loss.z));
    if(alpha<=1.e-6) return 0;
    float4 low,high;
    // The reflection is tone mapped and blended premultiplied; alpha is the
    // light the surface takes from what lies behind it.
    tonemap(low,high,max(reflection,0),s_tonemap.Load(int3(0,0,0)).x);
    return float4(low.rgb,alpha);
}
#endif
