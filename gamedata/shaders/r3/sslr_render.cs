//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR pass 1/3 - trace
//
// Inputs:  s_position, s_surface, s_diffuse (G-buffer), s_image = previous frame colour
// Outputs: u_sslr      rgb = reflected colour (gamma), a = hit confidence (0 = miss)
//          u_sslr_data xyz = view-space hit position, w = roughness of this pixel
//
// One VNDF-jittered ray per pixel, marched in screen space with perspective-correct depth,
// refined by bisection. Port of IX-Ray's sslr_render.cs.hlsl (LVutner) - see sslr_common.h
// for what changed and why.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float4> u_sslr		: register( u0 );
RWTexture2D<float4> u_sslr_data	: register( u1 );

float sslr_scene_depth( float2 uv )
{
	return s_position.SampleLevel( smp_nofilter, uv, 0 ).z;
}

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if( any( float2( DTid.xy ) >= sslr_screen.xy ) )
		return;

	float4 G	= s_position.Load( int3( DTid.xy, 0 ) );
	float  z	= G.z;

	// Sky and HUD: nothing to trace, the consumer keeps its cubemap / SSS reflection
	[branch] if( sslr_is_sky( z ) || sslr_is_hud( z ) )
	{
		u_sslr[DTid.xy]			= 0;
		u_sslr_data[DTid.xy]	= 0;
		return;
	}

	float4 S		= s_surface.Load( int3( DTid.xy, 0 ) );
	float  gloss	= s_diffuse.Load( int3( DTid.xy, 0 ) ).w;
	float  rough	= sslr_roughness( S, gloss );

	[branch] if( rough > SSLR_ROUGHNESS_CUTOFF )
	{
		u_sslr[DTid.xy]			= 0;
		u_sslr_data[DTid.xy]	= float4( 0, 0, 0, rough );
		return;
	}

	float2 uv	= ( float2( DTid.xy ) + 0.5 ) * sslr_screen.zw;
	float3 P	= sslr_view_pos( uv, z );
	float3 N	= normalize( sslr_unpack_normal( G.xy ) );
	float3 Vd	= normalize( P );			// eye -> point
	float2 rnd	= sslr_noise2( DTid.xy, sslr_params.w );

	// Importance-sampled reflection direction (IX-Ray: VNDF, biased against fireflies)
	float3 H	= sslr_sample_vndf( N, -Vd, rnd, rough * rough );
	float3 R	= reflect( Vd, H );
	if( dot( R, N ) <= 0.0 )
		R = normalize( R + N );				// IX-Ray: keep the ray above the surface

	// Ray segment in view space, kept in front of the near plane
	float3 P0	= P + N * ( 0.02 + 0.002 * z );
	float  len	= max( sslr_params.z, 1.0 );
	if( R.z < 0.0 )
		len = min( len, ( P0.z - 0.1 ) / -R.z );
	float3 P1	= P0 + R * len;

	float2 uv0	= sslr_project( P0 );
	float2 uv1	= sslr_project( P1 );
	float  iz0	= rcp( P0.z );
	float  iz1	= rcp( P1.z );

	// Clip the segment to the screen so no steps are wasted outside it
	float2 d	= uv1 - uv0;
	float  tmax	= 1.0;
	tmax		= d.x > 0.0 ? min( tmax, ( 1.0 - uv0.x ) / d.x ) : ( d.x < 0.0 ? min( tmax, -uv0.x / d.x ) : tmax );
	tmax		= d.y > 0.0 ? min( tmax, ( 1.0 - uv0.y ) / d.y ) : ( d.y < 0.0 ? min( tmax, -uv0.y / d.y ) : tmax );
	tmax		= saturate( tmax );

	float	t_prev	= 0.0;
	float	t_hit	= -1.0;
	float	jitter	= sslr_ign( float2( DTid.yx ), sslr_params.w + 7.0 );

	[loop]
	for( uint i = 0; i < SSLR_STEPS; ++i )
	{
		float t		= pow( ( float( i ) + jitter ) / float( SSLR_STEPS ), SSLR_STEP_POW ) * tmax;
		float2 suv	= uv0 + d * t;
		float rz	= rcp( lerp( iz0, iz1, t ) );
		float sz	= sslr_scene_depth( suv );

		// Ignore sky and HUD samples (HUD depth is in a different projection)
		if( !sslr_is_sky( sz ) && !sslr_is_hud( sz ) )
		{
			float dz	= rz - sz;
			float thick	= SSLR_THICKNESS_ABS + SSLR_THICKNESS_REL * sz;
			if( dz > 0.0 && dz < thick )
			{
				t_hit = t;
				break;
			}
		}
		t_prev = t;
	}

	float4 result	= 0;
	float3 Ph		= 0;

	[branch] if( t_hit >= 0.0 )
	{
		// Bisection between the last miss and the first hit
		float ta = t_prev, tb = t_hit;
		[unroll]
		for( uint k = 0; k < SSLR_REFINE_STEPS; ++k )
		{
			float tm	= 0.5 * ( ta + tb );
			float rz	= rcp( lerp( iz0, iz1, tm ) );
			float sz	= sslr_scene_depth( uv0 + d * tm );
			if( rz > sz )	tb = tm;
			else			ta = tm;
		}

		float2 huv	= uv0 + d * tb;
		float4 HG	= s_position.SampleLevel( smp_nofilter, huv, 0 );
		Ph			= sslr_view_pos( huv, HG.z );

		// Reject hits on back faces (ray entered an object from behind)
		float3 HN	= sslr_unpack_normal( HG.xy );
		float facing = saturate( -dot( HN, R ) * 4.0 + 0.5 );

		// Fade: screen edges, end of the ray, roughness approaching the cutoff
		float conf	= sslr_edge_fade( huv ) * facing;
		conf		*= 1.0 - smoothstep( 0.8, 1.0, tb );
		conf		*= 1.0 - smoothstep( SSLR_ROUGHNESS_CUTOFF * 0.6, SSLR_ROUGHNESS_CUTOFF, rough );

		// The colour comes from last frame's image: reproject the hit point
		float2 puv	= sslr_reproject( Ph );
		[flatten] if( !sslr_on_screen( puv ) )
		{
			puv		= huv;
			conf	*= 0.5;
		}
		conf		*= sslr_edge_fade( puv ) > 0.0 ? 1.0 : 0.0;

		result.rgb	= s_image.SampleLevel( smp_rtlinear, puv, 0 ).rgb;
		result.a	= saturate( conf );
	}

	u_sslr[DTid.xy]			= result;
	u_sslr_data[DTid.xy]	= float4( Ph, rough );
}
