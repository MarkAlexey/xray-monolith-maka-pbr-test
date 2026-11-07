#ifndef SUN_FAR_MINMAX_H
#define SUN_FAR_MINMAX_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: far sun cascade min/max tiles (r4_sun_far_minmax, LVutner's suggestion)
//
// The far light pass (accum_sun_far.ps) calls sun_far_shadow( PS ) instead of shadow( PS ).
// It reads one texel of s_sun_far_minmax (sun_far_minmax.cs): the min and max shadow-map depth
// in a window of at least 8 texels around the pixel, more than the shadow filter reads.
//   pixel depth <= min                      : every texel the filter can read passes: fully lit (1)
//   pixel depth - SUN_FAR_MINMAX_BIAS > max  : every texel fails: fully shadowed (0)
//   otherwise                                : the installed filter, shadow( PS ), as before
// The shadow sampler compares LESS_EQUAL (lit when the pixel's depth <= the stored depth); the
// filters only subtract a small bias from the pixel's depth (SSS: 0.000085, its PCSS blocker
// search 0.0001), which SUN_FAR_MINMAX_BIAS covers with a wide margin. The result is therefore
// the same as the filter's: only pixels at shadow edges, and the 1% next to the shadow map's
// border (SUN_FAR_MINMAX_EDGE), still run it.
// Off (always the filter) when the exe has no tiles (USE_SUN_FAR_MINMAX undefined), when
// r4_sun_far_minmax is 0, or on a light pass the tiles were not built for (the scope's second
// viewport on the bodycam branch): sun_far_minmax_params.x is 0 then.
// Include after shadow.h.
//////////////////////////////////////////////////////////////////////////////////////////

#define SUN_FAR_MINMAX_BIAS 0.0005	// shadow-map depth units
#define SUN_FAR_MINMAX_EDGE 0.01	// fraction of the shadow map next to its border left to the filter

#ifdef USE_SUN_FAR_MINMAX
Texture2D<float2> s_sun_far_minmax;
uniform float4 sun_far_minmax_params;	// x: 1 = the tiles match this light pass
#endif

// 1 = fully lit, 0 = fully shadowed, -1 = the shadow filter decides
float sun_far_minmax_class( float4 tc )
{
#ifdef USE_SUN_FAR_MINMAX
	[branch] if( sun_far_minmax_params.x > 0.5 )
	{
		const float3 p = tc.xyz / tc.w;
		if( all( p.xy >= SUN_FAR_MINMAX_EDGE ) && all( p.xy <= 1.0 - SUN_FAR_MINMAX_EDGE ) )
		{
			uint w, h;
			s_sun_far_minmax.GetDimensions( w, h );
			const int2 t = min( int2( p.xy * float2( w, h ) ), int2( w, h ) - 1 );
			const float2 mm = s_sun_far_minmax.Load( int3( t, 0 ) );
			if( p.z <= mm.x )
				return 1.0;
			if( p.z - SUN_FAR_MINMAX_BIAS > mm.y )
				return 0.0;
		}
	}
#endif
	return -1.0;
}

float sun_far_shadow( float4 tc )
{
	const float c = sun_far_minmax_class( tc );
	[branch] if( c >= 0.0 )
		return c;
	return shadow( tc );
}

#endif	//	SUN_FAR_MINMAX_H
