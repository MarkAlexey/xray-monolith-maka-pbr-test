//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR pass 3/3 - temporal accumulation
//
// Inputs:  s_image = spatially resolved SSLR of this frame, s_refl = last frame's final SSLR
//          (history, $user$sslr_old), s_position (G-buffer)
// Output:  u_sslr  final SSLR, read by combine_1 as s_refl (then copied into the history)
//
// History is fetched at the previous-frame position of this pixel's surface and clamped to
// the 3x3 neighbourhood of the current result (IX-Ray's HistoryClamp, simplified to a
// min/max box). Port of IX-Ray's sslr_temporal.cs.hlsl, with its duplicated (-1, -1)
// neighbour fixed to (+1, -1).
//
// Caveats:
//  - Reprojection assumes a static scene: reflections of moving objects smear slightly.
//    Lower r4_sslr_temporal if that bothers you.
//  - Reflections of distant objects move with parallax that surface reprojection does not
//    follow; the neighbourhood clamp hides most of it.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float4> u_sslr	: register( u0 );

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if( any( float2( DTid.xy ) >= sslr_screen.xy ) )
		return;

	float4 G		= s_position.Load( int3( DTid.xy, 0 ) );
	float4 current	= s_image.Load( int3( DTid.xy, 0 ) );

	[branch] if( sslr_is_sky( G.z ) || sslr_is_hud( G.z ) )
	{
		u_sslr[DTid.xy] = 0;
		return;
	}

	// 3x3 neighbourhood box
	int2 maxp	= int2( sslr_screen.xy ) - 1;
	float4 bmin	= current;
	float4 bmax	= current;
	[unroll]
	for( int y = -1; y <= 1; ++y )
	[unroll]
	for( int x = -1; x <= 1; ++x )
	{
		if( x == 0 && y == 0 )
			continue;
		float4 n	= s_image.Load( int3( clamp( int2( DTid.xy ) + int2( x, y ), int2( 0, 0 ), maxp ), 0 ) );
		bmin		= min( bmin, n );
		bmax		= max( bmax, n );
	}

	float2 uv	= ( float2( DTid.xy ) + 0.5 ) * sslr_screen.zw;
	float3 P	= sslr_view_pos( uv, G.z );
	float2 puv	= sslr_reproject( P );

	float weight = saturate( sslr_params.y );
	[branch] if( sslr_on_screen( puv ) && weight > 0.0 )
	{
		float4 hist	= s_refl.SampleLevel( smp_rtlinear, puv, 0 );
		hist		= clamp( hist, bmin, bmax );
		weight		*= sslr_edge_fade( puv ) > 0.0 ? 1.0 : 0.0;
		current		= lerp( current, hist, weight );
	}

	u_sslr[DTid.xy] = float4( max( current.rgb, 0.0 ), saturate( current.a ) );	// colour is HDR, not clamped to 1
}
