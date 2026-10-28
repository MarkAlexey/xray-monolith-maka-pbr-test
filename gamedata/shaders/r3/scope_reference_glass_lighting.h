// Indirect-light coefficients of three.js MeshPhysicalMaterial for the glass material.
#ifndef SCOPE_REFERENCE_GLASS_LIGHTING_H
#define SCOPE_REFERENCE_GLASS_LIGHTING_H
#include "scope_reference_brdf.h"
void scope_reference_multiscatter(float3 F0,float F90,float2 fab,out float3 single,out float3 multi) {
 single=F0*fab.x+F90*fab.y;
 float Ems=1-fab.x-fab.y;
 float3 Favg=F0+(1-F0)*scope_reference_average_fresnel;
 multi=single*Favg/(1-Ems*Favg)*Ems;
}
// geometryRoughness widens the lobe where the normal varies within a pixel.
float scope_reference_glass_roughness(ScopeReferenceGlass material,float geometryRoughness) {
 return min(max(material.roughness,scope_reference_min_roughness)+geometryRoughness,1);
}
// The specular single and multiple scattering, and the weight of the diffuse term (times baseColor).
void scope_reference_glass_specular(ScopeReferenceGlass material,float3 N,float3 V,float geometryRoughness,
 out float3 single,out float3 multi,out float3 diffuseWeight) {
 float roughness=scope_reference_glass_roughness(material,geometryRoughness);
 float metallic=material.metallic;
 float3 F0=.08*material.specular;
 float F90=lerp(2*material.specular,1,metallic);
 float2 fab=scope_reference_sample_dfg(roughness,saturate(dot(N,V)));
 float3 dielectricSingle,dielectricMulti,metalSingle,metalMulti;
 scope_reference_multiscatter(F0,F90,fab,dielectricSingle,dielectricMulti);
 scope_reference_multiscatter(material.baseColor,F90,fab,metalSingle,metalMulti);
 single=lerp(dielectricSingle,metalSingle,metallic);
 multi=lerp(dielectricMulti,metalMulti,metallic);
 diffuseWeight=(1-metallic)*(1-dielectricSingle-dielectricMulti);
}
void scope_reference_glass_coefficients(ScopeReferenceGlass material,float3 N,float3 V,
 float geometryRoughness,out float3 single,out float3 indirectDiffuse) {
 float3 multi,diffuseWeight;
 scope_reference_glass_specular(material,N,V,geometryRoughness,single,multi,diffuseWeight);
 indirectDiffuse=multi+material.baseColor*diffuseWeight;
}
#endif
