// Lens glass shading of the deferred path (scope_glass_compose.ps, scope_glass_compose_nvg.ps).
// Everything here is the glass material's own: its environment and VSLR local reflection, the
// player's head and shoulders in that reflection, the sun and the two strongest object lights, and
// the optical image where the pupil is clear. The result is linear scene radiance; the caller hands it
// to the installed display transfer through public functions only (no installed shader source is
// compiled into these passes).
#ifndef SCOPE_GLASS_COMPOSE_H
#define SCOPE_GLASS_COMPOSE_H
#include "scope_glass_authoring.h"
#include "svp_lens_object_light.h"
// The weather cubes and sky colour, as the deferred combine binds them (hmodel.h is not included).
TextureCube env_s0;
TextureCube env_s1;
uniform float4 env_color;
#include "scope_glass_gamma_environment.h"
#include "scope_glass_live_environment.h"
#include "scope_glass_material_payload.h"
#include "scope_glass_source_direct.h"
struct ScopeReferenceGlass {float3 baseColor,worldNormal,emission;float metallic,specular,roughness;};
#include "scope_reference_glass_lighting.h"
#include "scope_reference_glass_lights.h"
#include "vslr_trace.h"
#include "scope_glass_head.h"

Texture2D s_scope_glass_motion; // motion vectors; w is the glass tag and roughness
Texture2D scope_reference_ocular;
// x: glass drawn this frame, y: optical image ready, z: tonemap input gamma, w: r__svp_lens_debug
float4 scope_glass_deferred_control;
float4 scope_glass_source17,scope_glass_source18;
float4 scope_glass_hud_ray_scale; // world projection / HUD projection, XY
float4 scope_glass_vslr; // xyz: capture centre, w: radial limit; zero when no capture is published
float4 scope_glass_vslr_fade; // x: weight of the published capture over the previous one

struct ScopeGlassPixel
{
 float3 P;        // world position
 float3 N,V;      // world normal and direction to the eye
 float roughness;
 float hemi;
 ScopeReferenceGlass material;
};

// Glass pixels and their material; false elsewhere.
bool scope_glass_load(float2 pixel,out ScopeGlassPixel g)
{
 g=(ScopeGlassPixel)0;
 if(scope_glass_deferred_control.x<.5) return false;
 float tag=s_scope_glass_motion.Load(int3(int2(pixel),0)).w;
 if(tag<=1.5) return false;
 float sourceRoughness=svp_lens_dirt_roughness(scope_glass_decode_roughness(tag),scope_glass_source17,scope_glass_source18);
 gbuffer_data gbd=gbuffer_load_data(pixel*screen_res.zw,pixel);
 // The G-buffer holds HUD view depth; XY is rebuilt with the world projection.
 float3 hudP=float3(gbd.P.xy*scope_glass_hud_ray_scale.xy,gbd.P.z);
 g.P=mul(m_inv_V,float4(hudP,1)).xyz;
 g.N=normalize(mul((float3x3)m_inv_V,gbd.N));
 g.V=normalize(mul((float3x3)m_inv_V,-hudP));
 g.hemi=gbd.hemi;
 g.material.baseColor=SRGBToLinear(max(gbd.C,0));
 g.material.roughness=saturate(sourceRoughness);
 g.material.metallic=scope_glass_source_metallic(sourceRoughness,scope_glass_source17,scope_glass_source18);
 g.material.specular=saturate(scope_glass_source17.w);
 g.roughness=scope_reference_glass_roughness(g.material,0);
 return true;
}

// Mip of the capture for a reflection lobe, by the weather cubes' rule so the captured scene and the
// sky blur alike.
float scope_glass_capture_lod(float roughness)
{
 uint width,height,levels;
 vslr_colour.GetDimensions(0,width,height,levels);
 return scope_glass_lobe_lod(width,levels,roughness);
}

// Radiance arriving along a reflected ray: the captured static scene within the capture radius, the
// prefiltered environment behind it. Sharp lobes trace the capture for parallax; from the capture's
// second mip the reflected direction is looked up directly, where coverage fades edges as the lobe
// widens. A new capture fades in over the previous one on that wide path only.
float3 scope_glass_reflected_radiance(ScopeGlassPixel g,float3 direction,float lookupRoughness)
{
 float3 environment=scope_glass_live_environment_radiance(direction,lookupRoughness);
 if(scope_glass_vslr.w<=0) return environment;
 float lod=scope_glass_capture_lod(lookupRoughness);
 float3 wide=vslr_lobe_faded(direction,lod,environment,scope_glass_vslr_fade.x);
 // Fully wide from the capture's second mip.
 float sharpness=1-saturate(lod*.5);
 [branch] if(sharpness<=0) return wide;
 VslrHit hit=vslr_trace(g.P-scope_glass_vslr.xyz,direction,scope_glass_vslr.w,vslr_hit_tolerance);
 // Where the trace finds no surface, the capture's own sky along the ray, as on the wide path.
 float3 background=vslr_lobe(direction,lod,environment);
 float3 sharp=hit.confidence>0 ? lerp(background,vslr_lobe(hit.direction,lod,environment),hit.confidence) : background;
 return lerp(wide,sharp,sharpness);
}

// frontTerm is the coated specular reflection, diffuseTerm the multiple-scattering remainder, shooter the
// coverage of the player's body in the reflection.
void scope_glass_ambient_terms(ScopeGlassPixel g,out float3 frontTerm,out float3 diffuseTerm,out float shooter)
{
 shooter=0;
 float3 irradiance=scope_glass_gamma_irradiance(g.N,g.hemi);
 float lookupRoughness=svp_lens_blur_roughness(g.roughness);
 float3 reflected=reflect(-g.V,g.N);
 float3 radiance=scope_glass_reflected_radiance(g,reflected,lookupRoughness);
 // The player's body model covers the part of the reflection it blocks; without its mirror image the
 // sphere figure's dimly lit surface stands in.
 float lobe=scope_glass_head_lobe(lookupRoughness);
 [branch] if(scope_glass_mirror_origin.w>.5)
 {
  float4 body=scope_glass_mirror_ray(g.P,reflected,lobe);
  radiance=body.rgb+(1-body.a)*radiance;
  shooter=body.a;
 }
 else
 {
  float3 bodyNormal;
  float blocked=scope_glass_body_blocked(g.P,reflected,lobe,bodyNormal);
  [branch] if(blocked>0)
   radiance=lerp(radiance,scope_glass_head_params.x*scope_glass_gamma_irradiance(bodyNormal,g.hemi),blocked);
 }
 // Of the indirect diffuse light only the multiple scattering: the material's Lambert term would lay a
 // diffuse sheet over the whole glass.
 float3 single,multi,diffuseWeight;
 scope_reference_glass_specular(g.material,g.N,g.V,0,single,multi,diffuseWeight);
 frontTerm=radiance*scope_glass_coating_hue(single,saturate(dot(g.N,g.V)))*svp_lens_environment_scale();
 diffuseTerm=multi*irradiance*svp_lens_environment_scale();
}

float3 scope_glass_local_direct(ScopeGlassPixel g,float4 position,float4 color,float4 direction)
{
 float3 delta=position.xyz-g.P;float d2=dot(delta,delta);
 if(position.w<=0 || color.w<=0 || d2<=1.e-12) return 0;
 float3 L=delta*rsqrt(d2);
 return scope_glass_coated_direct(g.material.baseColor,g.material.metallic,g.material.specular,g.roughness,g.N,g.V,L)
  *scope_reference_light_incident(color,direction,position.w,d2,L)*scope_glass_head_light_visibility(g.P,L,sqrt(d2));
}

// Direct highlights: the coated specular lobe for the sun, shadowed by the owner's sun visibility,
// and for the two strongest lights that reach the owner, each hidden where the player's head or
// shoulders lie between. No diffuse term.
float3 scope_glass_direct(ScopeGlassPixel g)
{
 float3 result=0;
 float sunLength2=dot(L_sun_dir_w,L_sun_dir_w);
 if(sunLength2>1.e-12)
 {
  float3 sun=-L_sun_dir_w*rsqrt(sunLength2);
  result+=scope_glass_coated_direct(g.material.baseColor,g.material.metallic,g.material.specular,g.roughness,
   g.N,g.V,sun)*L_sun_color.rgb*svp_optic_sun_visibility()*scope_glass_head_light_visibility(g.P,sun,1.e6);
 }
 result+=scope_glass_local_direct(g,scope_glass_light0_position,scope_glass_light0_color,scope_glass_light0_direction);
 result+=scope_glass_local_direct(g,scope_glass_light1_position,scope_glass_light1_color,scope_glass_light1_direction);
 return result*svp_lens_glint_scale();
}

// The lit glass with the optical image where the pupil is clear. With the player shown, the coated
// reflection also stays over the optical image, as an eyepiece reflects over what it transmits, and
// the player's silhouette dims the optical image by r__svp_lens_shooter_ads.
float3 scope_glass_resolve(float3 lit,float2 pixel,float3 frontTerm,float shooter)
{
 // Debug 6 marks the glass footprint.
 if(scope_glass_deferred_control.w>5.5 && scope_glass_deferred_control.w<6.5) return float3(1,0,1);
 if(scope_glass_deferred_control.y<.5) return lit;
 float4 ocular=scope_reference_ocular.Load(int3(int2(pixel),0));
 float3 opticalLinear=SRGBToLinear(pow(max(ocular.rgb,0),1/scope_glass_deferred_control.z));
 // The optical image already carries pupil attenuation; its alpha replaces the reflection
 // where the pupil is clear.
 float pupil=saturate(ocular.a);
 float3 overlay=0;
 // The player rule (head_params.y) is steady over a frame without a mirror image.
 if(scope_glass_head_params.y>.5)
 {
  overlay=frontTerm*pupil;
  opticalLinear*=1-saturate(shooter*scope_glass_head_params.z);
 }
 return lit*(1-pupil)+opticalLinear+overlay;
}

// Linear radiance of a glass pixel. With ambient false (the night vision path) the installed combine adds
// its own ambient diffuse light afterwards, so the multiple-scattering remainder is left out; the
// reflection, the player in it, direct light and the optical image stay.
float3 scope_glass_radiance(ScopeGlassPixel g,float2 pixel,bool ambient)
{
 float debug=scope_glass_deferred_control.w;
 // Debug 10 shows the capture straight through the glass, to compare with the scene around it; 11
 // the player's mirror image alone in the sharp reflection.
 if(debug>10.5) return scope_glass_mirror_origin.w>.5 ? scope_glass_mirror_ray(g.P,reflect(-g.V,g.N),0).rgb : 0;
 if(debug>9.5) return scope_glass_vslr.w>0 ? vslr_lobe(-g.V,0,0) : 0;
 float3 frontTerm,diffuseTerm;
 float shooter;
 scope_glass_ambient_terms(g,frontTerm,diffuseTerm,shooter);
 if(!ambient) diffuseTerm=0;
 // Debug 7 shows the coated reflection and 9 the multiple-scattering term; 8 is black.
 if(debug>=6.5 && debug<9.5)
  return debug<7.5 ? frontTerm : debug<8.5 ? 0 : diffuseTerm;
 return scope_glass_resolve(frontTerm+diffuseTerm+scope_glass_direct(g),pixel,frontTerm,shooter);
}
#endif
