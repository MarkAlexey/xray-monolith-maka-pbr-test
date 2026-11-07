#ifndef SUN_FAR_MINMAX_H
#define SUN_FAR_MINMAX_H

// Far sun cascade min/max tiles (r4_sun_far_minmax, built by sun_far_minmax.cs).
// sun_far_shadow() returns 1 when the pixel is nearer than every texel the filter can read,
// 0 when it is farther than all of them, and runs shadow() only in between (shadow edges and
// the map's border). Without the tiles (no USE_SUN_FAR_MINMAX, params.x 0) it is shadow().
// Include after shadow.h.

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
