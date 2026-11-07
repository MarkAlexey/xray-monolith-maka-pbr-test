//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: far sun cascade min/max tiles (r4_sun_far_minmax, LVutner's suggestion).
// Engine side: r4_rendertarget_accum_csm.cpp (phase_sun_far_minmax), read by sun_far_minmax.h.
//
// One dispatch, right after the far cascade is rendered into the sun shadow map (CSM mode) and
// before its light pass. Output: RG16 UNORM at a quarter of the shadow map's size, one texel per
// 4x4 shadow-map texels ("tile"):
//   r = the minimum depth of this tile and of the SFM_DILATE tiles around it on every side
//   g = the maximum depth over the same tiles
// The min is rounded down and the max up to 16 bits, so both stay conservative. With the default
// dilation of 2 tiles, the window around any pixel reaches at least 8 shadow-map texels on every
// side: more than the installed far shadow filters read (Screen Space Shaders' shadow_pcss 4x4
// taps, its PCSS blocker search and filter, about 5 texels).
// Each 8x8 group loads its 12x12 tiles (the 8x8 it writes plus the dilation border) into
// groupshared memory once, 16 texels per tile; tiles past the map's edge repeat the edge tile,
// like the shadow sampler's clamp addressing.
//////////////////////////////////////////////////////////////////////////////////////////

#define SFM_TILE	4		// shadow-map texels per output texel, each axis
#define SFM_DILATE	2		// tiles folded in on every side
#define SFM_GROUP	8		// output texels per group, each axis
#define SFM_APRON	( SFM_GROUP + 2 * SFM_DILATE )

Texture2D<float> s_smap;							// the far cascade (depth)
RWTexture2D<float2> u_sun_far_minmax : register( u0 );	// r = min, g = max

groupshared float2 g_tile[ SFM_APRON * SFM_APRON ];

[numthreads( SFM_GROUP, SFM_GROUP, 1 )]
void main( uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex )
{
	uint2 src_size, dst_size;
	s_smap.GetDimensions( src_size.x, src_size.y );
	u_sun_far_minmax.GetDimensions( dst_size.x, dst_size.y );

	const int2 first = int2( gid.xy ) * SFM_GROUP - SFM_DILATE;	// first tile of the apron
	const int2 last_tile = int2( dst_size ) - 1;
	const int2 last_texel = int2( src_size ) - 1;

	// Load: min / max of every apron tile (64 threads, 144 tiles)
	for( uint i = gi; i < SFM_APRON * SFM_APRON; i += SFM_GROUP * SFM_GROUP )
	{
		const int2 tile = clamp( first + int2( i % SFM_APRON, i / SFM_APRON ), 0, last_tile );
		const int2 t0 = tile * SFM_TILE;
		float mn = 1.0, mx = 0.0;
		[unroll]
		for( int y = 0; y < SFM_TILE; ++y )
		{
			[unroll]
			for( int x = 0; x < SFM_TILE; ++x )
			{
				const float d = s_smap.Load( int3( min( t0 + int2( x, y ), last_texel ), 0 ) );
				mn = min( mn, d );
				mx = max( mx, d );
			}
		}
		g_tile[ i ] = float2( mn, mx );
	}
	GroupMemoryBarrierWithGroupSync();

	const uint2 o = gid.xy * SFM_GROUP + gtid.xy;
	if( any( o >= dst_size ) )
		return;

	// Fold the dilation window
	float mn = 1.0, mx = 0.0;
	[unroll]
	for( int dy = 0; dy <= 2 * SFM_DILATE; ++dy )
	{
		[unroll]
		for( int dx = 0; dx <= 2 * SFM_DILATE; ++dx )
		{
			const float2 v = g_tile[ ( gtid.y + dy ) * SFM_APRON + gtid.x + dx ];
			mn = min( mn, v.x );
			mx = max( mx, v.y );
		}
	}

	// Conservative 16-bit: exact multiples of 1/65535, so the UNORM store keeps them as written
	u_sun_far_minmax[ o ] = float2( floor( mn * 65535.0 ), ceil( mx * 65535.0 ) ) / 65535.0;
}
