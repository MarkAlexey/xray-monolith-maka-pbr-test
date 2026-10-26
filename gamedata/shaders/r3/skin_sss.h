#ifndef SKIN_SSS_H
#define SKIN_SSS_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: screen-space skin subsurface scattering for PBR first-person models (hands)
//
// Jorge Jimenez's separable SSS (Jimenez et al. 2015, "Separable Subsurface Scattering"): the
// diffuse light reaching the skin is blurred across the surface with the skin diffusion profile,
// once horizontally and once vertically, red the furthest. Specular stays sharp.
//
// Which pixels: PBR pixels with a subsurface value (bump# blue channel, or SKIN_SSS in the
// texture's .ltx, see pbr_hud.h) on first-person models (view depth below SKIN_SSS_HUD_DEPTH,
// the HUD's depth range, the same test SSLR uses).
//
// How (engine: r4_rendertarget_phase_skin_sss.cpp, console r4_skin_sss*):
//  1. Light passes (pbr_accum() in pbr_ixray.h) store skin pixels in the light accumulator as
//     rgb = diffuse irradiance (light x (1 - Fresnel), albedo NOT applied yet) and
//     a = -( specular luminance + SKIN_SSS_MARK ). The negative alpha marks the pixel as skin for
//     the blur; legacy and other PBR pixels keep their usual contents (alpha >= 0).
//  2. skin_sss_blur_h.ps: horizontal blur of the skin irradiance -> rt_skin_sss_tmp (RGBA16F,
//     a = view depth, 0 = not skin). skin_sss_blur_v.ps: vertical -> rt_skin_sss (R11G11B10F).
//  3. combine_1 (skin_sss_resolve): radiance = blurred irradiance x albedo + specular, the
//     specular colour taken from the unblurred irradiance (exact for one light; with several
//     coloured lights on the same pixel it is their mix).
//
// Off (r4_skin_sss 0, MSAA, or no SM 5): USE_SKIN_SSS is not defined, PBR_SKIN_SSS neither, and
// every function below compiles to what the shaders did before.
//
// Caveats:
//  - Only direct light scatters. The ambient (IBL) diffuse added in combine_1 does not (it is
//    smooth already); neither does the r2 static sun (r2_sun off), added in combine_1 as well.
//  - A skin pixel no light pass reaches at all (alpha 0) is not marked: it is neither blurred
//    nor used as a neighbour (the centre colour stands in for it). Shadowed pixels are still
//    marked: the sun pass covers the whole screen. The mark also keeps light shaders from an
//    older overlay (plain radiance, alpha 0) from being resolved as split skin light.
//  - The blur uses point sampling (no filtering across skin / non-skin) and drops neighbours
//    that are further away in depth than SKIN_SSS_DEPTH_FADE widths (fingers in front of the
//    palm), replacing them with the centre colour (Jimenez's "follow surface").
//  - Each light adds SKIN_SSS_MARK to the specular it stores; one is taken off again, so N
//    lights on a pixel leave (N - 1) x SKIN_SSS_MARK of extra specular (1e-4 linear each).
//  - A world surface closer than SKIN_SSS_HUD_DEPTH (a face pressed against the camera) with a
//    subsurface value is treated as first-person too, with the HUD's projection.
//////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////
// Settings (shaders recompile on restart)

// Blur taps per pass: 11, 17 or 25 (tools/make_skin_sss_kernel.py writes those kernels)
#ifndef SKIN_SSS_TAPS
#	define SKIN_SSS_TAPS 17
#endif

// Depth range of first-person models (view space). SSLR's value when it is installed.
#ifndef SKIN_SSS_HUD_DEPTH
#	ifdef SSLR_HUD_DEPTH
#		define SKIN_SSS_HUD_DEPTH SSLR_HUD_DEPTH
#	else
#		define SKIN_SSS_HUD_DEPTH 1.2
#	endif
#endif

// Subsurface values below this are not skin (bump# blue near black after compression)
#ifndef SKIN_SSS_MIN
#	define SKIN_SSS_MIN 0.02
#endif

// Follow surface: a neighbour further away in depth than FADE_START scatter widths starts to be
// replaced by the centre colour, beyond FADE_END it is replaced completely
#ifndef SKIN_SSS_DEPTH_FADE_START
#	define SKIN_SSS_DEPTH_FADE_START 0.5
#endif
#ifndef SKIN_SSS_DEPTH_FADE_END
#	define SKIN_SSS_DEPTH_FADE_END 2.0
#endif

// Skin marker added to the stored specular (see the caveats)
#define SKIN_SSS_MARK 1e-4

//////////////////////////////////////////////////////////////////////////////////////////

#if defined(USE_SKIN_SSS) && !defined(USE_MSAA) && defined(PBR_GBUFFER)
#	define PBR_SKIN_SSS
#endif

// x = scatter width (view-space metres, r4_skin_sss_width), y = strength (0 = off, 1 = full,
// r4_skin_sss_strength), z = unused, w = debug view (r4_skin_sss_debug)
uniform float4 skin_sss_params;
// HUD projection scale, view-space units -> UV: x = P._11 * 0.5, y = P._22 * 0.5
// (world projection in zw, for later use)
uniform float4 skin_sss_proj;

float skin_sss_luma( float3 c ) { return dot( c, float3( 0.2126, 0.7152, 0.0722 ) ); }

// sss = pbr_material.sss (or decoded from s_surface.w), z = view depth
bool skin_sss_is_skin( float sss, float z )
{
	return sss > SKIN_SSS_MIN && z > 0.0 && z < SKIN_SSS_HUD_DEPTH;
}

float skin_sss_surface_sss( float4 surface )
{
	return surface.w > 0.25 ? saturate( surface.w * 2.0 - 1.0 ) : 0.0;
}

#ifdef PBR_SKIN_SSS

Texture2D s_skin_sss;	// rt_skin_sss: blurred skin irradiance (combine_1)

// combine_1: the direct radiance of a pixel from the accumulator value L. Skin pixels get the
// blurred irradiance times the albedo plus the specular; every other pixel returns L.rgb.
float3 skin_sss_resolve( float sss, float3 diffuse, float3 P, float4 L, float2 pos2d )
{
	// L.a < 0: a light pass stored the pixel split (pbr_accum). Without the mark the pixel holds
	// plain radiance (no light reached it, or light shaders from an older overlay) and is kept.
	[branch] if( !skin_sss_is_skin( sss, P.z ) || L.a >= 0.0 )
		return L.rgb;

	float3 E	= L.rgb;
	float3 Eb	= s_skin_sss.Load( int3( pos2d, 0 ) ).rgb;
	Eb			= lerp( E, Eb, saturate( skin_sss_params.y ) );

	float spec	= max( 0.0, -L.a - SKIN_SSS_MARK );
	float lum	= skin_sss_luma( E );
	float3 S	= lum > 1e-6 ? spec * E / lum : (float3)spec;

	float3 result = Eb * diffuse + S;

	// r4_skin_sss_debug: 1 = skin mask (red = subsurface value), 2 = blurred irradiance,
	// 3 = what the scattering changed (x 4)
	[branch] if( skin_sss_params.w > 0.5 )
	{
		if( skin_sss_params.w < 1.5 )		result = lerp( result, float3( 1.0, 0.0, 0.0 ), 0.5 * sss );
		else if( skin_sss_params.w < 2.5 )	result = Eb;
		else								result = abs( Eb - E ) * 4.0;
	}
	return result;
}

#endif	// PBR_SKIN_SSS

#endif	// SKIN_SSS_H
