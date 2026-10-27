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
// 6 = direct light only, 7 = SSS Indirect Light factor (SSS only; grey = no change)
// #define PBR_DEBUG_VIEW 1

// SSS Indirect Light (bounce light) on PBR pixels, SSS only: scales the ambient diffuse by the
// same factor SSS uses for legacy surfaces. 0 = off, 1 = as SSS (default), >1 = stronger.
// #define PBR_IL_INTENSITY 1.0

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

// IX-Ray's translucency strength. IX-Ray 1.6: saturate( 3.5 * x + 0.1 ), whose 0.1 floor lights
// the far side of every subsurface pixel ("glowing bushes"); IX-Ray develop (c83074a7) uses
// saturate( 1.5 * x ). PBR_TRANSLUCENCY_GLOW_FIX 1 (default) = develop's, 0 = 1.6's.
// The fixed curve is also weaker overall (about 0.4x where it is not clamped).
#ifndef PBR_TRANSLUCENCY_GLOW_FIX
#	define PBR_TRANSLUCENCY_GLOW_FIX 1
#endif
float pbr_translucency_curve( float x )
{
#if PBR_TRANSLUCENCY_GLOW_FIX
	return saturate( 1.5 * x );
#else
	return saturate( 3.5 * x + 0.1 );
#endif
}

// IX-Ray's cheap wrap-around translucency, used for the sun on SSS materials
float3 pbr_translucency( pbr_material M, float3 color, float3 L, float3 N )
{
	float NdotL		= dot( L, N );
	float scale		= 0.36 * NdotL;

	float att		= scale + 0.0769; att *= att * 1.171;
	float factor	= 1.0 - saturate( abs( scale ) * 13.0 - 1.0 );

	float sss		= lerp( saturate( NdotL ), att, factor * factor );
	return pbr_to_linear( color * pbr_translucency_curve( sss ) ) * M.sss * M.diffuse;
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
// The same lights split into diffuse and specular, for the light accumulator (pbr_accum).
// Skin subsurface scattering (skin_sss.h) blurs the diffuse part of skin pixels before the
// albedo is applied, so it must reach the accumulator separately. For every other pixel
// pbr_accum() returns diff * albedo + spec, which is exactly what the float3 functions above
// return (ixr_brdf() = lerp( diffuse, specular, F ) = diffuse * ( 1 - F ) + specular * F).

//////////////////////////////////////////////////////////////////////////////////////////
// Skin diffuse ramp (experiment): soft, tinted shadow line for PBR pixels with a subsurface
// value, per light, in the light passes. Replaces N.L in the diffuse of those pixels (and the sun's
// wrap-around translucency, pbr_translucency_irradiance, which would wrap a second time).
// Independent of the screen-space blur (skin_sss.h, r4_skin_sss): works on world skin too, with
// MSAA, and without the blur passes. With both on they overlap at the shadow line: use a
// smaller PBR_SKIN_RAMP_WIDTH then.
//   0 = off (N.L, as before)
//   1 = ProcRamp: N.L blurred by a Gaussian (pre-integrated skin, Penner style), the light that
//       wraps past the shadow line tinted and faded by Beer-Lambert absorption over its path
//   2 = LVutner's sss(): N.L plus a fixed warm band at the shadow line (cheaper, artistic; adds
//       light on the lit side too, up to about +0.13 at N.L 0.5, so it is not energy conserving)
// Caveats:
//  - Both take the SIGNED N.L and their result is not clamped: the wrap lives at N.L < 0.
//  - Shadow maps still multiply the light afterwards, so self-shadowing cuts the wrap where the
//    shadow map darkens the far side; the screen-space blur softens that edge if it is on.
//  - Not applied to the r2 static sun (r2_sun off), which combine_1 lights with pbr_light_sun().
#ifndef PBR_SKIN_RAMP
#	define PBR_SKIN_RAMP 1
#endif
// Pixels with a subsurface value below this keep N.L
#ifndef PBR_SKIN_RAMP_MIN
#	define PBR_SKIN_RAMP_MIN 0.02
#endif
// 1: Gaussian width (standard deviation, in N.L units) at subsurface 1; scaled by the pixel's value
#ifndef PBR_SKIN_RAMP_WIDTH
#	define PBR_SKIN_RAMP_WIDTH 0.3
#endif
// 1: absorption per unit path, linear rgb: low red = red light gets furthest into the shadow
//    (green / blue: haemoglobin-like, as the HUD blood film's)
#ifndef PBR_SKIN_RAMP_ABSORB
#	define PBR_SKIN_RAMP_ABSORB float3( 1.0, 3.17, 3.63 )
#endif
// 2: LVutner's "ir" at subsurface 1 (scaled by the pixel's value; below 0.04 it does nothing)
//    and the band's colour
#ifndef PBR_SKIN_RAMP_LV_IR
#	define PBR_SKIN_RAMP_LV_IR 0.5
#endif
#ifndef PBR_SKIN_RAMP_LV_TINT
#	define PBR_SKIN_RAMP_LV_TINT float3( 1.0, 0.4, 0.1 )
#endif

bool pbr_skin_ramp_on( pbr_material M )
{
	return PBR_SKIN_RAMP != 0 && M.sss > PBR_SKIN_RAMP_MIN;
}

// ProcRamp: ndl signed (not clamped), o = Gaussian width
float3 pbr_proc_ramp( float ndl, float o )
{
	o			= max( o, 1e-7 );
	float rO	= 1.0 / o;
	float lmb	= max( ndl, 0.0 );
	float NvO	= lmb * rO;
	float AvO	= abs( ndl ) * rO;

	float A		= exp( -0.5 * NvO * NvO ) * ( o * 0.398942281 );	// o x normal pdf
	float x		= min( NvO * 0.417193, 1.0 );
	float B		= 0.5 + x * ( 1.0 - 0.5 * x );						// approximate normal CDF

	float C		= AvO + A;											// path length proxy
	float3 D	= exp( -PBR_SKIN_RAMP_ABSORB * C );					// Beer-Lambert absorption

	return lmb * B + D * ( A * o );
}

// LVutner's skin approximation: ndl signed (not clamped)
float3 pbr_lv_ramp( float ndl, float ir )
{
	float pndl	= saturate( ndl );
	float nndl	= saturate( -ndl );
	float back	= pow( max( 1.0 - nndl, 0.0 ), 3.0 / ( ir + 0.01 ) );
	return pndl + PBR_SKIN_RAMP_LV_TINT * 0.53 * ( 1.0 - pndl ) * ( 1.0 - pndl ) * back * saturate( ir - 0.04 );
}

// Diffuse N.L term of the light functions below: N.L, or the ramp for subsurface pixels.
// ndl signed.
float3 pbr_diffuse_ndl( pbr_material M, float ndl )
{
#if PBR_SKIN_RAMP == 1
	[branch] if( pbr_skin_ramp_on( M ) )
		return pbr_proc_ramp( ndl, PBR_SKIN_RAMP_WIDTH * M.sss );
#elif PBR_SKIN_RAMP == 2
	[branch] if( pbr_skin_ramp_on( M ) )
		return pbr_lv_ramp( ndl, PBR_SKIN_RAMP_LV_IR * M.sss );
#endif
	return max( ndl, 0.0 );
}

struct pbr_split
{
	float3	diff;	// diffuse irradiance x ( 1 - Fresnel ), linear, albedo NOT applied
	float3	spec;	// specular radiance, linear
};

pbr_split pbr_split_diffuse( float3 irradiance )
{
	pbr_split S;
	S.diff	= irradiance;
	S.spec	= 0;
	return S;
}

pbr_split pbr_direct_split( pbr_material M, float3 color, float3 L, float3 N, float3 V )
{
	float3 H	= normalize( L + V );

	float NdotL	= max( 0.0, dot( N, -L ) );
	float NdotH	= max( 0.0, dot( N, -H ) );
	float NdotV	= max( 0.0, dot( N, -V ) );
	float HdotV	= max( 0.0, dot( H, V ) );

	float  D	= ixr_D_GGX( NdotH, M.roughness );
	float  G	= ixr_G_Smith( NdotL, NdotV, M.roughness );
	float3 F	= ixr_F_Schlick( M.specular, HdotV );
	float3 lc	= pbr_to_linear( color );

	pbr_split S;
	S.diff	= lc * pbr_diffuse_ndl( M, dot( N, -L ) ) * ( 1.0 - F );	// signed N.L (skin ramp)
	S.spec	= lc * NdotL * ( 0.25 * D * G ) * F;
	return S;
}

// pbr_translucency() / pbr_legacy_translucency() without the albedo: diffuse light only
float3 pbr_translucency_irradiance( pbr_material M, float3 color, float3 L, float3 N )
{
	float NdotL		= dot( L, N );
	float scale		= 0.36 * NdotL;

	float att		= scale + 0.0769; att *= att * 1.171;
	float factor	= 1.0 - saturate( abs( scale ) * 13.0 - 1.0 );

	float sss		= lerp( saturate( NdotL ), att, factor * factor );
	return pbr_to_linear( color * pbr_translucency_curve( sss ) ) * M.sss;
}

float3 pbr_legacy_translucency_irradiance( pbr_material M, float3 color, float3 L, float3 N, float3 V )
{
#ifdef PBR_UNIFIED
	[branch] if( M.flora )
		return pbr_to_linear( color ) * SSS( N, -V, -L );
#endif
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Skin transmission (IX-Ray develop d97c480b, after Barre-Brisebois & Bouchard / Green,
// "real-time approximations to subsurface scattering"): light passing through thin parts
// towards the eye (fingers or the edge of the hand held against a light), view dependent,
// which neither the blur (skin_sss.h) nor the ramps above produce. For PBR pixels with a
// subsurface value, every light (IX-Ray: sun only):
//     T = pow( saturate( dot( -V, L - N * DISTORTION ) ), POWER ) * SCALE
// plus, for the sun with the ramp off, IX-Ray's wrap term at the shadow line
//     saturate( ( N.L + WRAP ) / ( 1 + WRAP ) ) - saturate( N.L )
// which then replaces IX-Ray 1.6's translucency (as in develop for non-foliage materials).
// Both tinted by TINT, scaled by the subsurface value, added to the diffuse light (so the
// blur scatters them too).
// Caveats:
//  - No thickness map: a thick palm transmits as much as a thin finger edge. The subsurface
//    value is the only control per texture.
//  - Shadow maps still multiply it: where the hand shadows its own far side from the light,
//    the transmission is shadowed too, so it mostly shows on silhouettes and thin edges.
//  - PBR_SKIN_TRANSMIT 0 = off (as before).
#ifndef PBR_SKIN_TRANSMIT
#	define PBR_SKIN_TRANSMIT 1
#endif
#ifndef PBR_SKIN_TRANSMIT_DISTORTION
#	define PBR_SKIN_TRANSMIT_DISTORTION 0.3		// how far the normal bends the light direction
#endif
#ifndef PBR_SKIN_TRANSMIT_POWER
#	define PBR_SKIN_TRANSMIT_POWER 4.0			// higher = tighter around looking into the light
#endif
#ifndef PBR_SKIN_TRANSMIT_SCALE
#	define PBR_SKIN_TRANSMIT_SCALE 0.25
#endif
#ifndef PBR_SKIN_TRANSMIT_WRAP
#	define PBR_SKIN_TRANSMIT_WRAP 0.5
#endif
#ifndef PBR_SKIN_TRANSMIT_TINT
#	define PBR_SKIN_TRANSMIT_TINT float3( 1.0, 0.35, 0.2 )
#endif

bool pbr_skin_transmit_on( pbr_material M )
{
	return PBR_SKIN_TRANSMIT != 0 && M.sss > PBR_SKIN_RAMP_MIN;
}

// Diffuse irradiance (albedo not applied). L = light travel direction, V = eye -> point.
float3 pbr_skin_transmit_irradiance( pbr_material M, float3 color, float3 L, float3 N, float3 V, bool wrap )
{
	float T		= pow( saturate( dot( -V, L - N * PBR_SKIN_TRANSMIT_DISTORTION ) ), PBR_SKIN_TRANSMIT_POWER ) * PBR_SKIN_TRANSMIT_SCALE;
	float ndl	= dot( N, -L );
	float W		= wrap ? saturate( ( ndl + PBR_SKIN_TRANSMIT_WRAP ) * rcp( 1.0 + PBR_SKIN_TRANSMIT_WRAP ) ) - saturate( ndl ) : 0.0;
	return pbr_to_linear( color ) * PBR_SKIN_TRANSMIT_TINT * ( ( T + W ) * M.sss );
}

// pbr_light_local() split
pbr_split pbr_light_local_split( pbr_material M, float3 P, float3 N, float4 light_pos, float3 color )
{
	float3 L2P	= P - light_pos.xyz;
	float3 L	= normalize( L2P );
	N			= normalize( N );
	float3 V	= normalize( P );
	float att	= pbr_attenuation( L2P, light_pos.w );

	pbr_split S	= pbr_direct_split( M, color, L, N, V );
	float3 trans = pbr_skin_transmit_on( M ) ? pbr_skin_transmit_irradiance( M, color, L, N, V, false ) : (float3)0.0;
	S.diff		= ( S.diff + trans + pbr_legacy_translucency_irradiance( M, color, L, N, V ) ) * att;
	S.spec		*= att;
	return S;
}

// pbr_light_sun() split
pbr_split pbr_light_sun_split( pbr_material M, float3 P, float3 N, float3 light_dir, float3 color )
{
	N = normalize( N );
	float3 L = normalize( light_dir );
	float3 V = normalize( P );

	pbr_split S	= pbr_direct_split( M, color, L, N, V );
	// the skin ramp already wraps the light (pbr_diffuse_ndl): no second wrap from the translucency.
	// With skin transmission, its own wrap term (ramp off) replaces IX-Ray 1.6's translucency.
	float3 wrap;
	[branch] if( pbr_skin_transmit_on( M ) )
		wrap	= pbr_skin_transmit_irradiance( M, color, L, N, V, !pbr_skin_ramp_on( M ) );
	else
		wrap	= pbr_skin_ramp_on( M ) ? (float3)0.0 : pbr_translucency_irradiance( M, color, L, N );
	S.diff		+= wrap + pbr_legacy_translucency_irradiance( M, color, L, N, V );
	return S;
}

#include "skin_sss.h"	// screen-space skin subsurface scattering (USE_SKIN_SSS, r4_skin_sss)

// What a light pass writes to the accumulator for a PBR pixel. k = everything the light is
// multiplied by after the BRDF (shadow, projector colour, attenuation the caller applies).
//  - skin (skin_sss.h): rgb = diffuse irradiance, a = -( specular luminance + mark )
//  - otherwise: rgb = linear radiance, a = 0 (as before)
float4 pbr_accum( pbr_material M, float3 P, pbr_split S, float3 k )
{
	float3 d	= S.diff * k;
	float3 s	= S.spec * k;
#ifdef PBR_SKIN_SSS
	[branch] if( skin_sss_is_skin( M.sss, P.z ) )
		return float4( d, -( skin_sss_luma( s ) + SKIN_SSS_MARK ) );
#endif
	return float4( d * M.diffuse + s, 0 );
}

//////////////////////////////////////////////////////////////////////////////////////////
// Ambient / image based lighting (metalic_roughness_ambient.hlsli), combine_1 only.
// Needs env_s0, env_s1 and env_color from hmodel.h.

#ifdef HMODEL_H

TextureCube		sky_s0;
TextureCube		sky_s1;
// combine_1 patched for a mod that declares them itself skips its declaration (compat patch)
#define PBR_SKY_DECLARED

#ifdef USE_OFFSCREEN_REFLECTIONS
// IX-Ray VSLR: the surroundings captured around the camera as an octahedral panorama
// (vslr_common.h, r4_rendertarget_phase_vslr.cpp). Set by the engine with r4_vslr_enable 1.
#	include "vslr_common.h"
Texture2D		s_env_fwd;		// rgb = gamma colour x coverage, a = coverage (premultiplied)

// VSLR check: uncomment to make PBR reflections show ONLY the capture, sharp (mip 0), with
// magenta where nothing was captured (sky / beyond r4_vslr_far / fog). On a metal PBR object
// you then see the mirrored surroundings with magenta sky. Combine with PBR_DEBUG_VIEW 5
// (ambient only) to hide the direct light. Needs r4_vslr_enable 1; with VSLR off (or before
// the first full capture) the reflection is plain magenta.
// #define VSLR_DEBUG
uniform float4	vslr_params;	// x = intensity (0 until captured), y = mip count, z = capture distance

// LEWD: localized lookup (Szirmay-Kalos, Aszodi, Lazanyi, Premecz 2005, "Approximate Ray-Tracing on
// the GPU with Distance Impostors"). The panorama is a single probe at the camera: a plain direction
// lookup treats everything as infinitely far, so nearby walls slide across a reflection. s_env_dist
// holds, per panorama direction, the distance from the capture point to what was captured; the
// false-position iteration below finds where the reflected ray from THIS pixel meets that surface,
// and the panorama is read in the direction of the hit. vslr_center.w = refinement steps
// (r4_vslr_localize, 0 = plain lookup as in IX-Ray).
Texture2D<float>	s_env_dist;		// R16F, metres from vslr_center.xyz (10 km where nothing was drawn)
uniform float4		vslr_center;	// xyz = capture position (world), w = refinement steps

float vslr_dist( float3 d )
{
	return s_env_dist.SampleLevel( smp_nofilter, vslr_panorama_uv( normalize( d ) ), 0 );
}

// x = ray origin relative to the capture point, R = unit ray direction (world). Returns the hit
// approximation relative to the capture point (the paper's Hit(), false-position refinement).
float3 vslr_localize( float3 x, float3 R, int steps )
{
	float rl	= vslr_dist( R );						// |r|: the plain lookup's distance
	float dp	= rl - dot( x, R );						// perpendicular-surface guess
	float3 p	= x + R * dp;
	float ppp	= length( p ) / max( vslr_dist( p ), 1e-3 );
	float dun	= 0.0, dov = 0.0, pun = ppp, pov = ppp;
	if( ppp < 1.0 ) dun = dp; else dov = dp;
	float dl	= max( dp + rl * ( 1.0 - ppp ), 0.0 );	// planar-surface guess
	float3 l	= x + R * dl;
	[loop] for( int i = 0; i < steps; ++i )
	{
		float llp	= length( l ) / max( vslr_dist( l ), 1e-3 );
		float ddl;
		if( llp < 1.0 )		// undershooting: in front of the surface
		{
			dun = dl; pun = llp;
			ddl = ( dov == 0.0 ) ? rl * ( 1.0 - llp ) : ( dl - dov ) * ( 1.0 - llp ) / ( llp - pov );
		}
		else				// overshooting: behind it
		{
			dov = dl; pov = llp;
			ddl = ( dun == 0.0 ) ? rl * ( 1.0 - llp ) : ( dl - dun ) * ( 1.0 - llp ) / ( llp - pun );
		}
		dl	= max( dl + ddl, 0.0 );
		l	= x + R * dl;
	}
	return l;
}

// Panorama direction for a reflection from world position pw: localized for sharp reflections,
// faded back to the plain direction for rough ones (their blur hides the parallax, and the
// distance panorama has no mips)
float3 vslr_lookup_dir( float3 rw, float3 pw, float roughness )
{
	int steps	= int( vslr_center.w );
	[branch] if( steps <= 0 || roughness > 0.6 )
		return rw;
	float3 l	= vslr_localize( pw - vslr_center.xyz, rw, steps );
	float3 d	= dot( l, l ) > 1e-6 ? normalize( l ) : rw;
	return normalize( lerp( d, rw, smoothstep( 0.3, 0.6, roughness ) ) );
}
#endif
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

// P = view-space position of the shaded pixel (LEWD: for the localized VSLR lookup)
float3 pbr_specular_irradiance( float3 R, float hemi, float roughness, float3 P )
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

	float3 result	= lerp( sky, env, env_blend ) * hemi;

#ifdef USE_OFFSCREEN_REFLECTIONS
	// IX-Ray VSLR (CompureSpecularIrradance with USE_VIEW_REFLECTIONS): where the capture saw
	// geometry, reflect it; elsewhere the sky reflection. The capture already hides the sky
	// behind walls and roofs, so the sky part is only darkened by hemi x 3 there, as in IX-Ray.
	// Rough surfaces read a smaller mip (IX-Ray: 6 x roughness).
	{
#	ifdef VSLR_DEBUG
		float  vslr_lod	= 0.0;
#	else
		float  vslr_lod	= min( 6.0, max( vslr_params.y - 1.0, 0.0 ) ) * roughness;
#	endif
		// LEWD: localized direction (distance impostor), plain direction with r4_vslr_localize 0
		float3 pw		= mul( m_inv_V, float4( P, 1.0 ) ).xyz;
		float3 vdir		= vslr_lookup_dir( normalize( rw ), pw, roughness );
		float4 vslr		= s_env_fwd.SampleLevel( smp_linear, vslr_panorama_uv( vdir ), vslr_lod );
		float3 vslr_sky	= lerp( sky, env, env_blend ) * saturate( hemi * 3.0 );
		result			= lerp( result, vslr.rgb + vslr_sky * ( 1.0 - vslr.a ), vslr_params.x );
#	ifdef VSLR_DEBUG
		result			= vslr.rgb + float3( 1.0, 0.0, 1.0 ) * ( 1.0 - vslr.a );
#	endif
	}
#endif

#ifdef PBR_ES
	return result + L_ambient.rgb;	// Enhanced Shaders adds the ambient colour
#else
	return result;
#endif
}

// Without a position (callers that have none): the camera, i.e. the unlocalized lookup
float3 pbr_specular_irradiance( float3 R, float hemi, float roughness )
{
	return pbr_specular_irradiance( R, hemi, roughness, (float3)0.0 );
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
	float3 specular	= pbr_to_linear( sslr_blend( pbr_specular_irradiance( R, hemi, M.roughness, P ), P ) );
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
