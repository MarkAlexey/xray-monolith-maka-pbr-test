#ifndef SSLR_APPLY_H
#define SSLR_APPLY_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR, consumer side
//
// Included by the ambient/IBL code that runs inside combine_1 (pbr_ixray.h for PBR pixels,
// the patched Enhanced Shaders hmodel.h for pseudo-PBR pixels). It blends the resolved SSLR
// result over the cubemap specular radiance:
//
//     radiance = cubemap_radiance * (1 - k * sslr.a) + k * sslr.rgb,  k = r4_sslr_intensity
//
// (s_refl is premultiplied: rgb = colour * confidence, see sslr_temporal.cs.)
//
// Both sides are in the same space: the gamma-space radiance the legacy pipeline uses
// before SRGBToLinear()/pbr_to_linear(). So call sslr_blend() BEFORE converting to linear.
//
// Needs (from common.h, already included by every combine_1 variant):
// pos_decompression_params, smp_nofilter.
// Without USE_SSLR_REFLECTIONS (engine define, r4_sslr_reflections 0) everything here
// compiles to nothing and the shaders behave exactly as before.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_settings.h"

#ifdef USE_SSLR_REFLECTIONS

Texture2D		s_refl;			// resolved SSLR, premultiplied: rgb = gamma radiance * a, a = confidence
uniform float4	sslr_params;	// x = intensity, y = temporal weight, z = max distance, w = frame

// View-space position -> screen UV, the inverse of the engine's G-buffer reconstruction
// (gbuffer_load_data), so P maps back exactly onto the pixel it came from
float2 sslr_project_uv( float3 P )
{
	return ( P.xy / P.z + pos_decompression_params.xy ) / ( 2.0 * pos_decompression_params.xy );
}

// SSLR for a view-space position P of the pixel being shaded. a = 0 means "no SSLR here".
float4 sslr_fetch( float3 P )
{
	[branch] if( P.z < SSLR_HUD_DEPTH )
		return 0;	// HUD: different projection, SSLR skipped these pixels anyway

	float2 uv = sslr_project_uv( P );
	return s_refl.SampleLevel( smp_nofilter, uv, 0 ) * saturate( sslr_params.x );
}

// Blend SSLR over gamma-space cubemap radiance for the pixel at view position P
float3 sslr_blend( float3 radiance_gamma, float3 P )
{
	float4 r = sslr_fetch( P );
#if SSLR_DEBUG == 1
	return r.aaa;
#elif SSLR_DEBUG == 2
	return r.a > 1e-3 ? r.rgb / r.a : radiance_gamma;
#else
	return radiance_gamma * ( 1.0 - r.a ) + r.rgb;
#endif
}

#else	//	USE_SSLR_REFLECTIONS

float3 sslr_blend( float3 radiance_gamma, float3 P ) { return radiance_gamma; }

#endif	//	USE_SSLR_REFLECTIONS

#endif	//	SSLR_APPLY_H
