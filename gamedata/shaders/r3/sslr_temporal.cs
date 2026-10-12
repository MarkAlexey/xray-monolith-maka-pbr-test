//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR pass 3/3 - temporal accumulation
//
// Inputs:  s_image = spatially resolved SSLR of this frame (rgb colour, a confidence),
//          s_refl = last frame's final SSLR (history, $user$sslr_old, PREMULTIPLIED),
//          s_hit = this frame's trace data (xyz hit position, w roughness),
//          s_position / s_position_prev = this and the previous main-view frame's G-buffer
// Output:  u_sslr = final SSLR, PREMULTIPLIED (rgb = colour * confidence, a = confidence).
//          Read by sslr_apply.h in combine_1, then copied into the history.
//
// Port of IX-Ray's sslr_temporal.cs.hlsl (LVutner), with these fixes against ghosting:
//
//  1. Where the history is fetched. IX-Ray's rule, ported: rough reflections follow the
//     reflecting surface, but a mirror-like reflection moves with the VIRTUAL image of the hit
//     point (behind the mirror, along the view ray at |P| + hit distance). Following the surface
//     for a mirror (what this port did before) slides the reflected picture across the
//     history every time the camera moves: trails behind everything reflected in puddles,
//     wet floors and glass.
//  2. Disocclusion. The history is only used if the reflecting surface was visible at its
//     previous position (depth test against the previous G-buffer). Newly revealed surfaces and
//     moved objects start fresh instead of inheriting what was there.
//  3. Clamping. Variance clipping (mean +- SSLR_TEMPORAL_GAMMA sigma, Salvi 2016) inside the
//     3x3 min/max box; min/max alone is loose wherever hits and misses meet.
//  4. Premultiplied colour. Blending (colour, confidence) separately darkened reflections
//     when a hit met a miss (miss colour is 0) and left dark halos; premultiplied blending is
//     correct for both.
//
// Caveats:
//  - Without a velocity buffer, moving objects are handled only by the depth test and the
//    clamp: their reflections can still trail for a few frames. Lower r4_sslr_temporal if
//    that bothers you; 0 disables accumulation (noisier rough reflections).
//  - The virtual-image rule is exact for flat mirrors; on curved glossy surfaces it is an
//    approximation (as in IX-Ray), corrected by the clamp.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float4> u_sslr	: register( u0 );

float4 sslr_premultiply( float4 c ) { return float4( c.rgb * c.a, c.a ); }

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if( any( float2( DTid.xy ) >= sslr_screen.xy ) )
		return;

	float4 G		= s_position.Load( int3( DTid.xy, 0 ) );
	float4 current	= sslr_premultiply( s_image.Load( int3( DTid.xy, 0 ) ) );

	[branch] if( sslr_is_sky( G.z ) || sslr_is_hud( G.z ) )
	{
		u_sslr[DTid.xy] = 0;
		return;
	}

	// 3x3 neighbourhood statistics of the current (premultiplied) result
	int2 maxp	= int2( sslr_screen.xy ) - 1;
	float4 bmin	= current;
	float4 bmax	= current;
	float4 m1	= current;
	float4 m2	= current * current;
	[unroll]
	for( int y = -1; y <= 1; ++y )
	[unroll]
	for( int x = -1; x <= 1; ++x )
	{
		if( x == 0 && y == 0 )
			continue;
		float4 n	= sslr_premultiply( s_image.Load( int3( clamp( int2( DTid.xy ) + int2( x, y ), int2( 0, 0 ), maxp ), 0 ) ) );
		bmin		= min( bmin, n );
		bmax		= max( bmax, n );
		m1			+= n;
		m2			+= n * n;
	}
	float4 mean		= m1 / 9.0;
	float4 sigma	= sqrt( max( m2 / 9.0 - mean * mean, 0.0 ) );
	float4 cmin		= max( bmin, mean - SSLR_TEMPORAL_GAMMA * sigma );
	float4 cmax		= min( bmax, mean + SSLR_TEMPORAL_GAMMA * sigma );

	float  weight	= saturate( sslr_params.y );

	float2 uv		= ( float2( DTid.xy ) + 0.5 ) * sslr_screen.zw;
	float3 P		= sslr_view_pos( uv, G.z );

	// Where this surface point was last frame, and was it visible there (disocclusion)
	float  prev_z;
	float2 puv_s	= sslr_reproject( P, prev_z );
	bool   valid	= weight > 0.0 && sslr_was_visible( puv_s, prev_z );

	[branch] if( valid )
	{
		float4 hist	= s_refl.SampleLevel( smp_rtlinear, puv_s, 0 );

		// Mirror-like reflections: history of the virtual image point instead (see header)
		float4 hit	= s_hit.Load( int3( DTid.xy, 0 ) );
		bool   has_hit = dot( hit.xyz, hit.xyz ) > 0.0;
		float  spec	= has_hit ? 1.0 - smoothstep( SSLR_VIRTUAL_ROUGH_LO, SSLR_VIRTUAL_ROUGH_HI, hit.w ) : 0.0;
		[branch] if( spec > 0.0 )
		{
			float  plen	= length( P );
			float3 Pv	= P * ( ( plen + length( hit.xyz - P ) ) / max( plen, 1e-4 ) );
			float2 puv_v = sslr_reproject( Pv );
			[flatten] if( sslr_on_screen( puv_v ) )
				hist	= lerp( hist, s_refl.SampleLevel( smp_rtlinear, puv_v, 0 ), spec );
		}

		hist		= clamp( hist, cmin, cmax );

		// Trust the history less the further the surface moved: reprojection errors (moving
		// objects, curved mirrors, sub-pixel jitter) grow with motion
		float motion_px	= length( ( puv_s - uv ) * sslr_screen.xy );
		weight		*= lerp( 1.0, 0.5, saturate( motion_px / SSLR_TEMPORAL_MOTION_PX ) );
		weight		*= sslr_edge_fade( puv_s ) > 0.0 ? 1.0 : 0.0;

		current		= lerp( current, hist, weight );
	}

	// Premultiplied: rgb <= a * SSLR_HDR_MAX by construction
	u_sslr[DTid.xy] = float4( max( current.rgb, 0.0 ), saturate( current.a ) );
}
