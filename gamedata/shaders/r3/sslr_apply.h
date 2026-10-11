#ifndef SSLR_APPLY_H
#define SSLR_APPLY_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR, consumer side
//
// Included by the ambient/IBL code that runs inside combine_1 (pbr_ixray.h for PBR pixels,
// the patched Enhanced Shaders hmodel.h for pseudo-PBR pixels). It blends the resolved SSLR
// result over the cubemap specular radiance:
//
//     radiance = lerp(cubemap_radiance, sslr.rgb, sslr.a * r4_sslr_intensity)
//
// Both sides are in the same space: the gamma-space radiance the legacy pipeline uses
// before SRGBToLinear()/pbr_to_linear(). So call sslr_blend() BEFORE converting to linear.
//
// Needs (from common.h, already included by every combine_1 variant): m_P, smp_nofilter.
// Without USE_SSLR_REFLECTIONS (engine define, r4_sslr_reflections 0) everything here
// compiles to nothing and the shaders behave exactly as before.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_settings.h"

#ifdef USE_SSLR_REFLECTIONS

Texture2D		s_refl;			// resolved SSLR: rgb = gamma radiance, a = confidence
uniform float4	sslr_params;	// x = intensity, y = temporal weight, z = max distance, w = frame

// View-space position -> screen UV with the main camera projection
float2 sslr_project_uv( float3 P )
{
	float4 c = mul( m_P, float4( P, 1.0 ) );
	return c.xy * ( float2( 0.5, -0.5 ) / c.w ) + 0.5;
}

// SSLR for a view-space position P of the pixel being shaded. a = 0 means "no SSLR here".
float4 sslr_fetch( float3 P )
{
	[branch] if( P.z < SSLR_HUD_DEPTH )
		return 0;	// HUD: different projection, SSLR skipped these pixels anyway

	float2 uv = sslr_project_uv( P );
	float4 r  = s_refl.SampleLevel( smp_nofilter, uv, 0 );
	r.a		 *= saturate( sslr_params.x );
	return r;
}

// Blend SSLR over gamma-space cubemap radiance for the pixel at view position P
float3 sslr_blend( float3 radiance_gamma, float3 P )
{
	float4 r = sslr_fetch( P );
#if SSLR_DEBUG == 1
	return r.aaa;
#elif SSLR_DEBUG == 2
	return r.a > 0.0 ? r.rgb : radiance_gamma;
#else
	return lerp( radiance_gamma, r.rgb, r.a );
#endif
}

#else	//	USE_SSLR_REFLECTIONS

float3 sslr_blend( float3 radiance_gamma, float3 P ) { return radiance_gamma; }

#endif	//	USE_SSLR_REFLECTIONS

#endif	//	SSLR_APPLY_H
