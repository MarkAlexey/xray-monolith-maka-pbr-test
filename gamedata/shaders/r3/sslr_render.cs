//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR pass 1/3 - trace
//
// Inputs:  s_position, s_surface, s_diffuse (G-buffer), s_image = previous main-view frame
//          colour ($user$sslr_color), s_position_prev = its G-buffer positions
// Outputs: u_sslr      rgb = reflected colour (gamma, pre-tonemap HDR), a = hit confidence (0 = miss)
//          u_sslr_data xyz = view-space hit position, w = roughness of this pixel
//
// One VNDF-jittered ray per pixel, marched in screen space with perspective-correct depth,
// refined by bisection. With USE_SSLR_HIZ (r4_sslr_hiz) the march is a hierarchical walk of a
// min-depth pyramid instead (Stachowiak 2015, IX-Ray develop; sslr_hiz_trace below), with the
// same hit tests. Port of IX-Ray's sslr_render.cs.hlsl (LVutner) - see sslr_common.h
// for what changed and why.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float4> u_sslr		: register( u0 );
RWTexture2D<float4> u_sslr_data	: register( u1 );

float sslr_scene_depth( float2 uv )
{
	return s_position.SampleLevel( smp_nofilter, uv, 0 ).z;
}

#ifdef USE_SSLR_HIZ
Texture2D<float> s_hiz;	// min pyramid of -1 / view depth, all mips (sslr_hiz_copy / _reduce.cs)

// Hierarchical trace of the screen-space segment uv0 + d * t, t in [0, tmax], whose -1/z goes
// linearly from s0 to s1 (after IX-Ray develop's HiZTraceSSR, in -1/z instead of hardware
// depth). Where the ray is in front of a cell's nearest surface and leaves the cell before
// reaching it, it skips the cell and goes up a level; where it reaches that depth inside the
// cell it goes down a level; going below level 0 is a hit. Returns the hit parameter in t_out.
//
// The walk is in level-0 pixel units: the cell of a pixel at level n is pixel >> n, clamped to
// the last cell, whose border is the screen edge. That is exactly how sslr_hiz_reduce.cs builds
// odd-sized levels (the last row / column takes the extra texels). Scaling the UV by the level's
// size instead (IX-Ray's version) puts some pixels into the neighbouring cell on odd-sized
// levels and misses their surfaces. Checked on the CPU against a brute-force march: 2000 rays
// over odd and even buffer sizes agree, except one ray clipping a pixel corner for less than
// SSLR_HIZ_EPS.
float sslr_hiz_exit( float2 O, float2 invD, bool2 pos, float2 cell, int level, float2 size, float2 dims )
{
	float  sc	= float( 1u << uint( level ) );
	float2 lo	= cell * sc;
	float2 hi	= cell >= dims - 1.0 ? size : ( cell + 1.0 ) * sc;
	float2 pl	= ( pos ? hi : lo ) + ( pos ? SSLR_HIZ_EPS : -SSLR_HIZ_EPS );
	float2 tp	= ( pl - O ) * invD;
	return min( tp.x, tp.y );
}

bool sslr_hiz_trace( float2 uv0, float2 d, float s0, float s1, float tmax, out float t_out )
{
	uint2 isize;
	uint  levels;
	s_hiz.GetDimensions( 0, isize.x, isize.y, levels );
	float2 size	= float2( isize );
	int max_level = int( levels ) - 1;

	float3 O	= float3( uv0 * size, s0 );			// pixels, -1/z
	float3 D	= float3( d * size, s1 - s0 );
	D			= abs( D ) > 1e-7 ? D : ( D >= 0.0 ? 1e-7 : -1e-7 );	// keep the sign of tiny components
	float3 invD	= rcp( D );
	bool2  pos	= D.xy >= 0.0;

	// Leave the start pixel first, so the ray does not hit its own surface
	float  t	= sslr_hiz_exit( O.xy, invD.xy, pos, floor( O.xy ), 0, size, size );
	float3 ray	= O + D * t;

	int  level	= 0;
	uint it		= 0;
	[loop]
	while( level >= 0 && it < SSLR_HIZ_ITERATIONS && t < tmax )
	{
		float2 dims		= float2( max( isize >> uint( level ), uint2( 1, 1 ) ) );
		float2 cell		= min( floor( ray.xy / float( 1u << uint( level ) ) ), dims - 1.0 );
		float  zs		= s_hiz.Load( int3( cell, level ) );

		float t_cell	= sslr_hiz_exit( O.xy, invD.xy, pos, cell, level, size, dims );
		float t_surf	= D.z > 0.0 ? ( zs - O.z ) * invD.z : 1e20;

		bool above		= ray.z < zs;			// in front of the cell's nearest surface
		bool skip		= above && t_cell < t_surf;
		t				= above ? min( t_cell, t_surf ) : t;
		ray				= O + D * t;
		level			= min( level + ( skip ? 1 : -1 ), max_level );
		++it;
	}
	t_out = t;
	return level < 0 && t < tmax;
}
#endif

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
	bool	march	= true;

#ifdef USE_SSLR_HIZ
	{
		float th;
		march = false;
		[branch] if( sslr_hiz_trace( uv0, d, -iz0, -iz1, tmax, th ) )
		{
			// Same thickness test as the march: the pyramid has none of its own
			float rz	= rcp( lerp( iz0, iz1, th ) );
			float sz	= sslr_scene_depth( uv0 + d * th );
			float dz	= rz - sz;
			float thick	= SSLR_THICKNESS_ABS + SSLR_THICKNESS_REL * sz;
			if( !sslr_is_sky( sz ) && !sslr_is_hud( sz ) && dz > -thick && dz < thick )
			{
				t_hit	= th;
				t_prev	= th;	// exact already: the bisection below keeps it
			}
			else
				march	= SSLR_HIZ_FALLBACK != 0;	// went behind a thin object
		}
	}
#endif

	[loop]
	for( uint i = 0; march && i < SSLR_STEPS; ++i )
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

		// The colour comes from last frame's image: reproject the hit point, and only trust that
		// pixel if the hit point was actually visible there last frame. Otherwise the fetch
		// returns whatever covered it (most visibly the weapon, which then "ghosts" in floor and
		// puddle reflections), so the ray counts as a miss and the cubemap shows instead.
		float  prev_z;
		float2 puv	= sslr_reproject( Ph, prev_z );
		conf		*= sslr_was_visible( puv, prev_z ) ? 1.0 : 0.0;
		conf		*= sslr_edge_fade( puv ) > 0.0 ? 1.0 : 0.0;

		result.rgb	= sslr_untonemap( s_image.SampleLevel( smp_rtlinear, puv, 0 ).rgb );	// HDR, see SSLR_HDR_DECODE
		result.a	= saturate( conf );
	}

	u_sslr[DTid.xy]			= result;
	u_sslr_data[DTid.xy]	= float4( Ph, rough );
}
