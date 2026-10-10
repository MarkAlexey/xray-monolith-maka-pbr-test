#ifndef PBR_IXRAY_H
#define PBR_IXRAY_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray PBR (metallic/roughness) lighting
//
// Ported from IX-Ray 1.6 (https://github.com/ixray-team/ixray-1.6-stcop), mainly
// gamedata/shaders/d3d11/metalic_roughness_light.hlsli, metalic_roughness_ambient.hlsli and
// metalic_roughness_base.hlsli. All credit for the lighting model goes to the IX-Ray team.
//
// How it fits into the stock renderer:
//  - Only pixels written by textures marked "PBR (metallic/roughness)" in their THM use it.
//    The G-buffer target s_surface tells them apart (see gbuffer_stage.h).
//  - For those pixels the accumulator holds linear radiance (albedo and BRDF included, no
//    separate specular in alpha), and combine_1 adds IBL ambient and converts the result
//    back to the gamma space the rest of the pipeline expects.
//  - Everything else keeps the legacy lighting model untouched.
//
// View-space conventions used here (same as IX-Ray):
//  V = normalize(P)         eye -> point
//  L = light travel dir     light -> point (Ldynamic_dir for the sun)
//////////////////////////////////////////////////////////////////////////////////////////

#include "common.h"

#ifdef PBR_GBUFFER

//////////////////////////////////////////////////////////////////////////////////////////
// Options. Uncomment to change them for every PBR texture (shaders recompile on restart).

// IX-Ray's USE_NORMAL_HEMI_DISTRIBUTION: let the normal limit the hemisphere (sky) light, so
// downward-facing detail gets darker in shade. Off by default, as in IX-Ray. It also darkens
// vertical surfaces (sides of walls, weapons) noticeably.
// #define USE_NORMAL_HEMI_DISTRIBUTION

// IX-Ray's IBL_FAKE_IRRADANCE: always blend rough reflections toward the blurry hemisphere
// cubemap. Without it, that only happens when the sky cubemap has no mipmaps to blur with.
// #define IBL_FAKE_IRRADANCE

// Upper limit for matching the sky reflection's brightness to the ambient light (see
// pbr_specular_irradiance). Lower it if reflections of dark sky areas look too bright.
// #define PBR_REFLECTION_SCALE_MAX 16.0

// Debug view for PBR pixels, written by combine_1: 1 = world-space normal, 2 = ambient
// occlusion (material AO x SSAO), 3 = roughness, 4 = metalness, 5 = ambient light only,
// 6 = direct light only
// #define PBR_DEBUG_VIEW 1

#ifndef PBR_GAMMA
#	define PBR_GAMMA 2.2
#endif

// F0 for dielectrics. IX-Ray: F0_BASE 0.5, decoded as F0_BASE^2 * 0.16
#ifndef PBR_F0_DIELECTRIC
#	define PBR_F0_DIELECTRIC 0.04
#endif

float3 pbr_to_linear( float3 c ) { return pow( abs( c ), PBR_GAMMA ); }
float  pbr_to_linear( float c ) { return pow( abs( c ), PBR_GAMMA ); }
float3 pbr_to_gamma( float3 c ) { return pow( abs( c ), 1.0 / PBR_GAMMA ); }

struct pbr_material
{
	bool	enabled;	// false: legacy pixel, use the old lighting model
	float	roughness;
	float	ao;
	float	sss;
	float3	diffuse;	// linear, albedo * (1 - metalness)
	float3	specular;	// linear F0
};

pbr_material pbr_load( float2 tc, float2 pos2d, uint iSample, float3 albedo )
{
	float4 S = gbuffer_load_surface( tc, pos2d, iSample );

	pbr_material M;
	M.enabled	= S.w > 0.25;
	M.roughness	= max( 0.02, S.y );
	M.ao		= S.z;
	M.sss		= saturate( S.w * 2.0 - 1.0 );

	float3 lin	= pbr_to_linear( albedo );
	M.specular	= lerp( (float3)PBR_F0_DIELECTRIC, lin, S.x );
	M.diffuse	= lin * ( 1.0 - S.x );

	return M;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Direct lighting (metalic_roughness_light.hlsli)

float pbr_distribution_ggx( float NdotH, float roughness )
{
	float alpha		= roughness * roughness;
	float alpha2	= alpha * alpha + 1e-6;
	float divider	= NdotH * NdotH * ( alpha2 - 1.0 ) + 1.0;
	return alpha2 * rcp( divider * divider );
}

float pbr_geometry_smith( float NdotL, float NdotV, float roughness )
{
	float r = roughness + 1.0;
	float k = r * r * 0.125;
	return rcp( lerp( NdotL, 1.0, k ) ) * rcp( lerp( NdotV, 1.0, k ) );
}

float3 pbr_fresnel_schlick( float3 F0, float cosine )
{
	float F90 = min( 1.0, dot( F0, 16.66667 ) );
	return lerp( F0, F90, pow( 1.0 - cosine, 5.0 ) );
}

// Radiance reaching the eye from one light, linear. color is the light colour as the engine
// sends it (gamma space); attenuation and shadows are applied by the caller.
float3 pbr_direct( pbr_material M, float3 color, float3 L, float3 N, float3 V )
{
	float3 H	= normalize( L + V );

	float NdotL	= max( 0.0, dot( N, -L ) );
	float NdotH	= max( 0.0, dot( N, -H ) );
	float NdotV	= max( 0.0, dot( N, -V ) );
	float HdotV	= max( 0.0, dot( H, V ) );

	float  D	= pbr_distribution_ggx( NdotH, M.roughness );
	float  G	= pbr_geometry_smith( NdotL, NdotV, M.roughness );
	float3 F	= pbr_fresnel_schlick( M.specular, HdotV );

	float3 brdf	= lerp( M.diffuse, 0.25 * D * G, F );
	return pbr_to_linear( color ) * NdotL * brdf;
}

// IX-Ray's cheap wrap-around translucency, used for the sun on SSS materials
float3 pbr_translucency( pbr_material M, float3 color, float3 L, float3 N )
{
	float NdotL		= dot( L, N );
	float scale		= 0.36 * NdotL;

	float att		= scale + 0.0769; att *= att * 1.171;
	float factor	= 1.0 - saturate( abs( scale ) * 13.0 - 1.0 );

	float sss		= lerp( saturate( NdotL ), att, factor * factor );
	return pbr_to_linear( color * saturate( 3.5 * sss + 0.1 ) ) * M.sss * M.diffuse;
}

// IX-Ray GetLightAttention() with falloff 2, for point and spot lights
float pbr_attenuation( float3 light_to_point, float inv_range_sqr )
{
	float factor	= dot( light_to_point, light_to_point ) * inv_range_sqr;
	float att		= max( 1.0 - factor, 0.0 );
	return ( att * att ) / ( 1.0 + 2.0 * factor );
}

// Point/spot light without shadows or projector map
float3 pbr_light_local( pbr_material M, float3 P, float3 N, float4 light_pos, float3 color )
{
	float3 L2P	= P - light_pos.xyz;
	float3 L	= normalize( L2P );
	return pbr_direct( M, color, L, normalize( N ), normalize( P ) ) * pbr_attenuation( L2P, light_pos.w );
}

// Sun without shadows (shadow goes through pbr_to_linear() in the caller, as in IX-Ray)
float3 pbr_light_sun( pbr_material M, float3 P, float3 N, float3 light_dir, float3 color )
{
	N = normalize( N );
	float3 L = normalize( light_dir );
	return pbr_direct( M, color, L, N, normalize( P ) ) + pbr_translucency( M, color, L, N );
}

//////////////////////////////////////////////////////////////////////////////////////////
// Ambient / image based lighting (metalic_roughness_ambient.hlsli), combine_1 only.
// Needs env_s0, env_s1 and env_color from hmodel.h.

#ifdef HMODEL_H

TextureCube		sky_s0;
TextureCube		sky_s1;
uniform float4	L_sky_color;	// rgb = sky colour of the current weather, w = blend factor

// Split-sum environment BRDF, fitted for height-correlated Smith (IX-Ray)
float2 pbr_env_brdf_approx( float NdotV, float roughness )
{
	NdotV = min( NdotV, 0.998 );

	float nsqr = NdotV * NdotV;
	float rsqr = roughness * roughness;

	float4 fac = float4( 0.0187, 1.0133, 1.0000, 1.0000 ) +
	float4( 1.9496, -2.4717, -0.0333, 2.0508 ) * NdotV +
	float4( 1.2265, -1.2172, -1.3097, 0.2342 ) * roughness +
	float4( -7.6907, 3.4300, 0.5972, -26.9406 ) * NdotV * roughness +
	float4( 18.3314, 1.4794, 19.3537, 11.1429 ) * nsqr +
	float4( -0.2894, 0.5564, 1.5052, 7.0828 ) * rsqr +
	float4( -19.3056, -2.2456, -28.2302, 18.5470 ) * nsqr * roughness +
	float4( 7.0144, -1.8934, 1.3307, 50.6469 ) * NdotV * rsqr +
	float4( 1.5728, 1.3618, 15.2939, -63.3557 ) * nsqr * rsqr;

	return saturate( fac.xy / fac.zw );
}

// Diffuse irradiance in the same (gamma) space as the legacy hmodel(), so matte PBR and legacy
// surfaces under the same sky end up equally bright.
float3 pbr_diffuse_irradiance( float3 N, float hemi )
{
	float3 nw	= mul( m_inv_V, N );
#ifdef USE_NORMAL_HEMI_DISTRIBUTION
	hemi		= min( hemi, nw.y * 0.375 + 0.375 );
#endif
	float3 e0	= env_s0.SampleLevel( smp_rtlinear, nw, 0 ).rgb;
	float3 e1	= env_s1.SampleLevel( smp_rtlinear, nw, 0 ).rgb;
	float3 env	= env_color.rgb * lerp( e0, e1, env_color.w );
	return env * env * hemi + L_ambient.rgb;
}

// Specular irradiance from the sky cubemaps, blurred by roughness through the mip chain.
//
// Anomaly lights everything else with the hemisphere cubemap scaled by env_color (and squared),
// which is several times brighter than the raw sky texture times the sky colour. Reflecting the
// raw sky made metals (which have no diffuse term) far too dark next to everything else. So the
// sky only provides the shape and detail of the reflection: its brightness is rescaled to match
// the hemisphere cubemap around the same direction (the ratio of their luminances, the sky
// measured on a heavily blurred mip). Rough reflections therefore end up as bright as the
// ambient light, and smooth ones keep the sky's detail.
//
// A sky texture without mipmaps cannot be blurred, so then the hemisphere cubemap is blended in
// by roughness instead (IX-Ray's IBL_FAKE_IRRADANCE, which forces it always).
#ifndef PBR_REFLECTION_SCALE_MAX
#	define PBR_REFLECTION_SCALE_MAX 16.0	// limit for the sky -> hemisphere brightness match
#endif

float pbr_luminance( float3 c ) { return dot( c, float3( 0.2126, 0.7152, 0.0722 ) ); }

float3 pbr_specular_irradiance( float3 R, float hemi, float roughness )
{
	float3 rw	= mul( m_inv_V, R );

	float w, h, mips0, mips1;
	sky_s0.GetDimensions( 0, w, h, mips0 );
	sky_s1.GetDimensions( 0, w, h, mips1 );
	float lod0	= ( mips0 - 1.0 ) * roughness;
	float lod1	= ( mips1 - 1.0 ) * roughness;
	// A few texels per face: smooth enough to stand for the sky's local average brightness
	float avg0	= max( mips0 - 3.0, 0.0 );
	float avg1	= max( mips1 - 3.0, 0.0 );
#ifdef IBL_FAKE_IRRADANCE
	float env_blend	= roughness;
#else
	float env_blend	= min( mips0, mips1 ) > 1.0 ? 0.0 : roughness;
#endif

	float3 s0	= sky_s0.SampleLevel( smp_linear, rw, lod0 ).rgb;
	float3 s1	= sky_s1.SampleLevel( smp_linear, rw, lod1 ).rgb;
	float3 sky	= lerp( s0, s1, env_color.w ) * L_sky_color.rgb;

	float3 a0	= sky_s0.SampleLevel( smp_linear, rw, avg0 ).rgb;
	float3 a1	= sky_s1.SampleLevel( smp_linear, rw, avg1 ).rgb;
	float3 sky_avg	= lerp( a0, a1, env_color.w ) * L_sky_color.rgb;

	float3 e0	= env_s0.SampleLevel( smp_rtlinear, rw, 0 ).rgb;
	float3 e1	= env_s1.SampleLevel( smp_rtlinear, rw, 0 ).rgb;
	float3 env	= env_color.rgb * lerp( e0, e1, env_color.w );
	env			*= env;

	float scale	= pbr_luminance( env ) / max( pbr_luminance( sky_avg ), 1e-4 );
	sky			*= min( scale, PBR_REFLECTION_SCALE_MAX );

	return lerp( sky, env, env_blend ) * hemi;
}

// Linear ambient radiance (IX-Ray AmbientLightingImpl). occ = SSAO * material AO.
float3 pbr_ambient( pbr_material M, float3 P, float3 N, float hemi, float occ )
{
	N			= normalize( N );
	float3 V	= normalize( P );
	float3 R	= reflect( V, N );
	float NdotV	= max( 0.0, dot( N, -V ) );

	float3 diffuse	= pbr_to_linear( pbr_diffuse_irradiance( N, hemi ) ) * M.diffuse;
	float3 specular	= pbr_to_linear( pbr_specular_irradiance( R, hemi, M.roughness ) );

	float2 brdf	= pbr_env_brdf_approx( NdotV, M.roughness );
	float  F90	= min( 1.0, dot( M.specular, 16.66667 ) );
	float3 F	= M.specular * brdf.x + F90 * brdf.y;

	return occ * lerp( diffuse, specular, F );
}

#endif	//	HMODEL_H

#endif	//	PBR_GBUFFER

#endif	//	PBR_IXRAY_H
