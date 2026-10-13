#ifndef VSLR_COMMON_H
#define VSLR_COMMON_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray VSLR (offscreen reflections), shared by vslr_panorama.cs (writer) and
// pbr_ixray.h (reader). Engine side: r4_rendertarget_phase_vslr.cpp.
//
// The panorama is an octahedral map of world directions with +Y (up) at the centre, so the
// sky, which most reflections see, gets the most texels. Contents (RGBA16F, premultiplied so
// the mip chain averages correctly):
//   rgb = captured colour (gamma space, like the cubemap reflections) x coverage
//   a   = coverage: 1 = scene geometry, 0 = nothing captured (sky) or hidden by fog
//////////////////////////////////////////////////////////////////////////////////////////

// Gamma used to bring the linear capture into the reflection code's gamma space
#ifndef VSLR_GAMMA
#	define VSLR_GAMMA 2.2
#endif

float2 vslr_sign_not_zero( float2 v ) { return float2( v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0 ); }

// World direction -> [-1, 1]^2
float2 vslr_oct_encode( float3 n )
{
	n /= ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
	float2 p = n.xz;
	if( n.y < 0.0 )
		p = ( 1.0 - abs( p.yx ) ) * vslr_sign_not_zero( p );
	return p;
}

// [-1, 1]^2 -> world direction (normalised)
float3 vslr_oct_decode( float2 p )
{
	float3 n = float3( p.x, 1.0 - abs( p.x ) - abs( p.y ), p.y );
	if( n.y < 0.0 )
		n.xz = ( 1.0 - abs( n.zx ) ) * vslr_sign_not_zero( n.xz );
	return normalize( n );
}

// Texture coordinates of a world direction in the panorama
float2 vslr_panorama_uv( float3 dir_w )
{
	return vslr_oct_encode( dir_w ) * float2( 0.5, -0.5 ) + 0.5;
}

#endif	//	VSLR_COMMON_H
