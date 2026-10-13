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
//  - Everything else keeps the legacy lighting model untouched, unless Enhanced Shaders is
//    installed: then legacy textures get Enhanced Shaders' pseudo-PBR material and go through
//    this lighting too (see PBR_UNIFIED_LIGHTING below).
//
// View-space conventions used here (same as IX-Ray):
//  V = normalize(P)         eye -> point
//  L = light travel dir     light -> point (Ldynamic_dir for the sun)
//////////////////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "lmodel.h"	// Enhanced Shaders' lmodel.h brings its pseudo-PBR material conversion
#include "ixray_brdf.h"	// shared IX-Ray BRDF and cubemap remap (also used by patched Enhanced Shaders)
#include "sslr_apply.h"	// IX-Ray SSLR consumer (no-op unless r4_sslr_reflections 1)
#include "hdr_ibl.h"	// HDR highlights reconstructed from the 8-bit cubemaps

#ifdef PBR_GBUFFER

// Enhanced Shaders (and the SSS / Atmospherics edits of it) is installed when its
// pbr_settings.h and its material table are loaded
#if defined(ALBEDO_BOOST) && defined(MAT_FLORA_ELIPSON)
#	define PBR_ES
#endif

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

// Unified lighting, only with Enhanced Shaders installed. 1 (default): legacy textures get
// Enhanced Shaders' pseudo-PBR material (its own conversion and pbr_settings.h, so mods that
// tune it still apply) and are lit by this IX-Ray lighting together with real PBR textures,
// with the ambient light calibrated like Enhanced Shaders'. 0: legacy textures keep Enhanced
// Shaders' own lighting and only PBR textures use this one.
// #define PBR_UNIFIED_LIGHTING 0

#ifndef PBR_UNIFIED_LIGHTING
#	define PBR_UNIFIED_LIGHTING 1
#endif
// Atmospherics (2026 versions, by Hippobot) replaces Enhanced Shaders' material conversion
// (calc_albedo / calc_specular / calc_rough) with its own per-surface BRDFs
// (pbr_brdf_character.h, detected by its MTL_RANGE / OREN_ROUGH_LO). Unified lighting needs
// Enhanced Shaders' functions, so it is off there: PBR textures still get this IX-Ray lighting,
// legacy textures keep Atmospherics' own.
#if defined(MTL_RANGE) && defined(OREN_ROUGH_LO)
#	define PBR_ATMOS_BRDF
#endif
#if defined(PBR_ES) && PBR_UNIFIED_LIGHTING && !defined(PBR_ATMOS_BRDF)
#	define PBR_UNIFIED
#endif

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
	bool	legacy;		// unified lighting: a legacy texture with a converted material
	bool	flora;		// unified lighting: legacy flora (gets Enhanced Shaders' translucency)
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
	M.legacy	= false;
	M.flora		= false;
	M.roughness	= max( 0.02, S.y );
	M.ao		= S.z;
	M.sss		= saturate( S.w * 2.0 - 1.0 );

	float3 lin	= pbr_to_linear( albedo );
	M.specular	= lerp( (float3)PBR_F0_DIELECTRIC, lin, S.x );
	M.diffuse	= lin * ( 1.0 - S.x );

	return M;
}

#ifdef PBR_UNIFIED
// Enhanced Shaders' pseudo-PBR material for a legacy pixel, with the same conversion its
// lmodel.h / hmodel.h use (calc_* from the installed pbr_brdf.h). m is the material id the
// lighting shader uses (xmaterial with the static sun).
pbr_material pbr_from_legacy( float4 alb_gloss, float m )
{
#ifdef SSFX_NEWGLOSS
	// SSS "Improved gloss & wet surfaces": terrain is treated as a non-metal
	if( abs( m - 0.95 ) <= 0.04 )
		m = 0.0;
#endif
	float3 albedo	= calc_albedo( alb_gloss, m );
	float3 specular	= calc_specular( alb_gloss, m );
	float  rough	= calc_rough( alb_gloss, m );
	calc_foliage( albedo, specular, rough, alb_gloss, m );

	pbr_material M;
	M.enabled	= true;
	M.legacy	= true;
	M.flora		= abs( m - MAT_FLORA ) <= MAT_FLORA_ELIPSON;
	M.roughness	= max( 0.02, rough );	// perceptual roughness, as in IX-Ray
	M.ao		= 1.0;
	M.sss		= 0.0;
	M.diffuse	= albedo;	// already linear, metals included
	M.specular	= specular;
	return M;
}
#endif

// Material for any pixel: PBR textures as above; with unified lighting, legacy textures get
// the converted material, so every pixel takes the PBR path.
pbr_material pbr_load_unified( float2 tc, float2 pos2d, uint iSample, float4 alb_gloss, float m )
{
	pbr_material M = pbr_load( tc, pos2d, iSample, alb_gloss.rgb );
#ifdef PBR_UNIFIED
	[branch] if( !M.enabled )
		M = pbr_from_legacy( alb_gloss, m );
#endif
	return M;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Direct lighting (metalic_roughness_light.hlsli)

// Thin wrappers over ixray_brdf.h, kept so existing callers and patches still compile
float  pbr_distribution_ggx( float NdotH, float roughness )				{ return ixr_D_GGX( NdotH, roughness ); }
float  pbr_geometry_smith( float NdotL, float NdotV, float roughness )	{ return ixr_G_Smith( NdotL, NdotV, roughness ); }
float3 pbr_fresnel_schlick( float3 F0, float cosine )					{ return ixr_F_Schlick( F0, cosine ); }

// Radiance reaching the eye from one light, linear. color is the light colour as the engine
// sends it (gamma space); attenuation and shadows are applied by the caller.
float3 pbr_direct( pbr_material M, float3 color, float3 L, float3 N, float3 V )
{
	float3 H	= normalize( L + V );

	float NdotL	= max( 0.0, dot( N, -L ) );
	float NdotH	= max( 0.0, dot( N, -H ) );
	float NdotV	= max( 0.0, dot( N, -V ) );
	float HdotV	= max( 0.0, dot( H, V ) );

	float3 brdf	= ixr_brdf( M.diffuse, M.specular, M.roughness, NdotL, NdotH, NdotV, HdotV );
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

// Enhanced Shaders' flora translucency (SSS() from the installed common_brdf.h, including the
// SSS Flora Fixes version), for legacy flora under unified lighting. L and V as above.
float3 pbr_legacy_translucency( pbr_material M, float3 color, float3 L, float3 N, float3 V )
{
#ifdef PBR_UNIFIED
	[branch] if( M.flora )
	{
		float3 subsurface = SSS( N, -V, -L );
		return pbr_to_linear( color ) * subsurface * M.diffuse;
	}
#endif
	return 0;
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
	N			= normalize( N );
	float3 V	= normalize( P );
	return ( pbr_direct( M, color, L, N, V ) + pbr_legacy_translucency( M, color, L, N, V ) ) *
		pbr_attenuation( L2P, light_pos.w );
}

// Sun without shadows (shadow goes through pbr_to_linear() in the caller, as in IX-Ray)
float3 pbr_light_sun( pbr_material M, float3 P, float3 N, float3 light_dir, float3 color )
{
	N = normalize( N );
	float3 L = normalize( light_dir );
	float3 V = normalize( P );
	return pbr_direct( M, color, L, N, V ) + pbr_translucency( M, color, L, N ) +
		pbr_legacy_translucency( M, color, L, N, V );
}

//////////////////////////////////////////////////////////////////////////////////////////
// Ambient / image based lighting (metalic_roughness_ambient.hlsli), combine_1 only.
// Needs env_s0, env_s1 and env_color from hmodel.h.

#ifdef HMODEL_H

TextureCube		sky_s0;
TextureCube		sky_s1;
uniform float4	L_sky_color;	// rgb = sky colour of the current weather, w = blend factor
uniform float4	L_sky_rotation;	// x = cos, y = sin of the skybox rotation around Y

// World direction -> sky cubemap coordinates, matching how the engine draws the skybox
// (dxEnvironmentRender.cpp): the box is rotated by the weather's sky_rotation, and its texture
// coordinates put the horizon at the bottom edge of the side faces, so the visible upper half of
// the sky covers the whole cubemap (y * 2 - 1 on the box). Below the horizon the skybox shows
// that bottom edge, so the coordinates are clamped there.
float3 pbr_sky_coords( float3 w )
{
	float2 cs	= dot( L_sky_rotation.xy, L_sky_rotation.xy ) > 0.5 ? L_sky_rotation.xy : float2( 1.0, 0.0 );
	return ixr_sky_dir( w, cs );	// rotation + unfuck() + horizon clamp (IXR_SKY_CLAMP_HORIZON)
}

// World direction -> hemisphere cubemap coordinates for reflections, the same "fake remapping"
// the legacy hmodel() uses for its reflections
float3 pbr_env_coords( float3 w )
{
	return ixr_env_dir( w );	// unfuck(); identical to the old guarded remap (y = 1 maps to 1 either way)
}

// Split-sum environment BRDF, fitted for height-correlated Smith (IX-Ray)
float2 pbr_env_brdf_approx( float NdotV, float roughness ) { return ixr_env_brdf_approx( NdotV, roughness ); }

#ifdef PBR_ES
// Enhanced Shaders' hemisphere lighting (its hmodel.h, by KennShade): the hemisphere cubemaps
// sampled as a Valve-style ambient cube on their most blurred mip, so PBR and legacy surfaces
// get the same ambient brightness as Enhanced Shaders gives them.
float3 pbr_es_ambient_cube( TextureCube cube, float3 nw, float3 nr )
{
	const float eps	= 0.001;
	float3 n2		= nw * nw;
	float3 c		= 0;
	c	+= n2.x * cube.SampleLevel( smp_linear, float3( nr.x, eps, eps ), CUBE_MIPS ).rgb;
	c	+= n2.y * cube.SampleLevel( smp_linear, float3( eps, nr.y, eps ), CUBE_MIPS ).rgb;
	c	+= n2.z * cube.SampleLevel( smp_linear, float3( eps, eps, nr.z ), CUBE_MIPS ).rgb;
	return c;
}
#endif

// Diffuse irradiance in the same (gamma) space as the legacy hmodel(), so matte PBR and legacy
// surfaces under the same sky end up equally bright.
float3 pbr_diffuse_irradiance( float3 N, float hemi )
{
	float3 nw	= mul( m_inv_V, N );
#ifdef USE_NORMAL_HEMI_DISTRIBUTION
	hemi		= min( hemi, nw.y * 0.375 + 0.375 );
#endif
#ifdef PBR_ES
	nw			= normalize( nw );
	float3 nr	= pbr_env_coords( nw );
	nr			= normalize( nr );
	float3 e0	= pbr_es_ambient_cube( env_s0, nw, nr );
	float3 e1	= pbr_es_ambient_cube( env_s1, nw, nr );
	return env_color.rgb * lerp( e0, e1, env_color.w ) * hemi + L_ambient.rgb;
#else
	float3 e0	= env_s0.SampleLevel( smp_rtlinear, nw, 0 ).rgb;
	float3 e1	= env_s1.SampleLevel( smp_rtlinear, nw, 0 ).rgb;
	float3 env	= env_color.rgb * lerp( e0, e1, env_color.w );
	return env * env * hemi + L_ambient.rgb;
#endif
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

	float3 sc	= pbr_sky_coords( rw );
	float3 ec	= pbr_env_coords( rw );

	float3 s0	= sky_s0.SampleLevel( smp_linear, sc, lod0 ).rgb;
	float3 s1	= sky_s1.SampleLevel( smp_linear, sc, lod1 ).rgb;
	// HDR reflections: the 8-bit sky's highlights are expanded (hdr_ibl.h). The blurred average
	// below is expanded the same way, so the brightness match keeps the overall level and the
	// highlights end up brighter than the average, as in an HDR environment map.
	float3 sky	= hdr_ibl_expand_gamma( lerp( s0, s1, env_color.w ) ) * L_sky_color.rgb;

	float3 a0	= sky_s0.SampleLevel( smp_linear, sc, avg0 ).rgb;
	float3 a1	= sky_s1.SampleLevel( smp_linear, sc, avg1 ).rgb;
	float3 sky_avg	= hdr_ibl_expand_gamma( lerp( a0, a1, env_color.w ) ) * L_sky_color.rgb;

#ifdef PBR_ES
	// Enhanced Shaders: hemisphere cubemap blurred by roughness through its mips, not squared
	float  emip	= CUBE_MIPS - ( 1.0 - roughness ) * CUBE_MIPS;
	float3 e0	= env_s0.SampleLevel( smp_linear, ec, emip ).rgb;
	float3 e1	= env_s1.SampleLevel( smp_linear, ec, emip ).rgb;
	float3 env	= env_color.rgb * hdr_ibl_expand_gamma( lerp( e0, e1, env_color.w ) );
#else
	float3 e0	= env_s0.SampleLevel( smp_rtlinear, ec, 0 ).rgb;
	float3 e1	= env_s1.SampleLevel( smp_rtlinear, ec, 0 ).rgb;
	float3 env	= env_color.rgb * hdr_ibl_expand_gamma( lerp( e0, e1, env_color.w ) );
	env			*= env;
#endif

	// Below the horizon the reflection is the ground, not the sky: fade to the hemisphere
	// cubemap, which is what the legacy lighting reflects there
	env_blend	= max( env_blend, saturate( -rw.y * 4.0 ) );

	float scale	= pbr_luminance( env ) / max( pbr_luminance( sky_avg ), 1e-4 );
	sky			*= min( scale, PBR_REFLECTION_SCALE_MAX );

#ifdef PBR_ES
	return lerp( sky, env, env_blend ) * hemi + L_ambient.rgb;	// Enhanced Shaders adds the ambient colour
#else
	return lerp( sky, env, env_blend ) * hemi;
#endif
}

// Linear ambient radiance (IX-Ray AmbientLightingImpl). occ = SSAO * material AO.
// diffuse_scale multiplies the diffuse part only (SSS indirect light).
float3 pbr_ambient_ex( pbr_material M, float3 P, float3 N, float hemi, float occ, float3 diffuse_scale )
{
	N			= normalize( N );
	float3 V	= normalize( P );
	float3 R	= reflect( V, N );
	float NdotV	= max( 0.0, dot( N, -V ) );

	float3 diffuse	= pbr_to_linear( pbr_diffuse_irradiance( N, hemi ) ) * M.diffuse * diffuse_scale;
	// IX-Ray SSLR replaces the cubemap reflection where its rays hit something (gamma space,
	// before linearisation; see sslr_apply.h). P is the unnormalised view position.
	float3 specular	= pbr_to_linear( sslr_blend( pbr_specular_irradiance( R, hemi, M.roughness ), P ) );
#if defined(PBR_UNIFIED) && defined(SSFX_NEWGLOSS)
	// SSS "Improved gloss & wet surfaces": flora reflects a quarter of the environment
	specular		*= M.flora ? 0.25 : 1.0;
#endif

	return occ * lerp( diffuse, specular, ixr_env_F( M.specular, NdotV, M.roughness ) );
}

float3 pbr_ambient( pbr_material M, float3 P, float3 N, float hemi, float occ )
{
	return pbr_ambient_ex( M, P, N, hemi, occ, 1.0 );
}

#endif	//	HMODEL_H

#endif	//	PBR_GBUFFER

#endif	//	PBR_IXRAY_H
