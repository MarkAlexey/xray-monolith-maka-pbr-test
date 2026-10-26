#ifndef SKIN_SSS_BLUR_H
#define SKIN_SSS_BLUR_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: skin subsurface scattering blur (skin_sss.h), one direction per pass.
// SKIN_SSS_PASS 0 (skin_sss_blur_h.ps): light accumulator -> rt_skin_sss_tmp (rgb, a = depth)
// SKIN_SSS_PASS 1 (skin_sss_blur_v.ps): rt_skin_sss_tmp   -> rt_skin_sss (R11G11B10F)
// Engine: r4_rendertarget_phase_skin_sss.cpp. Non-skin pixels return 0 after one or two loads.
//////////////////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "sslr_settings.h"	// SSLR_HUD_DEPTH (first-person depth range)

#include "skin_sss.h"
#include "skin_sss_kernel.h"

#if SKIN_SSS_PASS == 0
// s_accumulator, s_position: common.h. s_surface: gbuffer_stage.h (via common.h)
#else
Texture2D s_skin_sss_tmp;
#endif

#ifdef USE_MSAA
// Not run with MSAA (the engine turns skin SSS off there); kept compilable
float4 main( p_screen I ) : SV_Target { return 0; }
#else

float skin_sss_follow( float zM, float z, float width )
{
	return smoothstep( SKIN_SSS_DEPTH_FADE_START, SKIN_SSS_DEPTH_FADE_END, abs( zM - z ) / max( width, 1e-5 ) );
}

float4 main( p_screen I ) : SV_Target
{
	int3 px = int3( I.hpos.xy, 0 );

	float sss	= skin_sss_surface_sss( s_surface.Load( px ) );
#if SKIN_SSS_PASS == 0
	float4 acc	= s_accumulator.Load( px );
	float zM	= s_position.Load( px ).z;
	float3 cM	= acc.rgb;
	[branch] if( !skin_sss_is_skin( sss, zM ) || acc.a >= 0.0 )	// not skin, or not stored split (skin_sss.h)
		return 0;
	float2 dir	= float2( skin_sss_proj.x, 0.0 );
#else
	float4 t	= s_skin_sss_tmp.Load( px );
	float zM	= t.a;
	float3 cM	= t.rgb;
	[branch] if( zM <= 0.0 )
		return 0;
	float2 dir	= float2( 0.0, skin_sss_proj.y );
#endif

	// Scatter width in view space -> UV at this depth (HUD projection), scaled by the pixel's
	// subsurface value; Jimenez's 1/3: the kernel offsets run to 2 or 3 widths
	float width	= skin_sss_params.x * sss;
	float2 step	= dir * ( width / zM ) * ( 1.0 / 3.0 );

	float3 sum	= cM * skin_sss_kernel[0].rgb;
	[unroll] for( int i = 1; i < SKIN_SSS_TAPS; ++i )
	{
		float2 uv	= I.tc0 + skin_sss_kernel[i].a * step;
#if SKIN_SSS_PASS == 0
		float4 a	= s_accumulator.SampleLevel( smp_nofilter, uv, 0 );
		float  z	= s_position.SampleLevel( smp_nofilter, uv, 0 ).z;
		bool   skin	= a.a < 0.0 && z > 0.0 && z < SKIN_SSS_HUD_DEPTH;
#else
		float4 a	= s_skin_sss_tmp.SampleLevel( smp_nofilter, uv, 0 );
		float  z	= a.a;
		bool   skin	= z > 0.0;
#endif
		float  f	= skin ? skin_sss_follow( zM, z, width ) : 1.0;
		sum			+= skin_sss_kernel[i].rgb * lerp( a.rgb, cM, f );
	}

#if SKIN_SSS_PASS == 0
	return float4( sum, zM );
#else
	return float4( sum, 1.0 );
#endif
}

#endif	// USE_MSAA

#endif
