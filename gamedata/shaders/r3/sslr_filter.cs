//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR pass 2/3 - spatial resolve
//
// Inputs:  s_image = trace result (rgb colour, a confidence), s_refl = trace data
//          (xyz hit position, w roughness), s_position (G-buffer)
// Output:  u_sslr_temp rgb = resolved colour, a = resolved confidence
//
// Reuses the rays of neighbouring pixels, weighted by how likely THIS pixel's GGX lobe would
// have produced them (IX-Ray's sslr_filter: D(NdotH) of the neighbour's hit as seen from
// here), plus depth and normal similarity so reflections do not bleed across edges. The
// radius grows with roughness; mirrors keep their own ray.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float4> u_sslr_temp	: register( u0 );

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if( any( float2( DTid.xy ) >= sslr_screen.xy ) )
		return;

	float4 center	= s_image.Load( int3( DTid.xy, 0 ) );
	float4 cdata	= s_refl.Load( int3( DTid.xy, 0 ) );
	float4 G		= s_position.Load( int3( DTid.xy, 0 ) );
	float  rough	= cdata.w;

	[branch] if( sslr_is_sky( G.z ) || sslr_is_hud( G.z ) || rough > SSLR_ROUGHNESS_CUTOFF || rough < 0.06 )
	{
		u_sslr_temp[DTid.xy] = center;	// nothing to resolve (or a mirror: keep its own ray)
		return;
	}

	float2 uv	= ( float2( DTid.xy ) + 0.5 ) * sslr_screen.zw;
	float3 P	= sslr_view_pos( uv, G.z );
	float3 N	= normalize( sslr_unpack_normal( G.xy ) );
	float3 V	= -normalize( P );		// to the eye

	float radius	= max( 1.0, SSLR_FILTER_RADIUS * rough );
	float rot		= sslr_ign( float2( DTid.xy ), sslr_params.w ) * 2.0 * PI;

	float4 sum_c	= 0;	// colour weighted by lobe * confidence, alpha unused
	float  sum_cw	= 0;
	float  sum_a	= 0;	// confidence weighted by geometry only
	float  sum_aw	= 0;

	[loop]
	for( uint i = 0; i < SSLR_FILTER_SAMPLES + 1; ++i )
	{
		// Sample 0 is the centre; the rest on a golden-angle spiral
		float  fi	= float( i );
		float  r	= i == 0 ? 0.0 : sqrt( fi / float( SSLR_FILTER_SAMPLES ) ) * radius;
		float  a	= fi * 2.39996323 + rot;
		int2   pix	= int2( DTid.xy ) + int2( round( float2( cos( a ), sin( a ) ) * r ) );
		pix			= clamp( pix, int2( 0, 0 ), int2( sslr_screen.xy ) - 1 );

		float4 s	= s_image.Load( int3( pix, 0 ) );
		float4 sd	= s_refl.Load( int3( pix, 0 ) );
		float4 sG	= s_position.Load( int3( pix, 0 ) );

		if( sslr_is_sky( sG.z ) || sslr_is_hud( sG.z ) )
			continue;

		// Geometry similarity
		float wz	= saturate( 1.0 - abs( sG.z - G.z ) / ( 0.05 * G.z + 0.05 ) );
		float wn	= pow( saturate( dot( sslr_unpack_normal( sG.xy ), N ) ), 8.0 );
		float wg	= wz * wn;

		sum_a		+= s.a * wg;
		sum_aw		+= wg;

		[branch] if( s.a > 0.0 )
		{
			// How likely this pixel's lobe produced the neighbour's hit direction
			float3 L	= sd.xyz - P;
			float  ll	= length( L );
			L			= ll > 1e-4 ? L / ll : N;
			float3 Hh	= normalize( L + V );
			float  NdotH = saturate( dot( N, Hh ) );
			float  w	= max( sslr_D_GGX( NdotH, rough ) * NdotH, 1e-5 ) * wg * s.a;

			sum_c.rgb	+= s.rgb * w;
			sum_cw		+= w;
		}
	}

	float4 o;
	o.rgb	= sum_cw > 0.0 ? sum_c.rgb / sum_cw : center.rgb;
	o.a		= sum_aw > 0.0 ? sum_a / sum_aw : 0.0;

	u_sslr_temp[DTid.xy] = float4( max( o.rgb, 0.0 ), saturate( o.a ) );	// colour is HDR, not clamped to 1
}
