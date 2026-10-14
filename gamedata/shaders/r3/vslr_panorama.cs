//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray VSLR panorama (r4_rendertarget_phase_vslr.cpp, CBlender_vslr element 0).
//
// IX-Ray's combine_vslr.ps (LVutner / IX-Ray team), as a compute shader: the two capture
// cubemaps (colour, distance) -> one octahedral panorama of world directions (vslr_common.h),
// mip 0. The engine builds the mip chain afterwards (GenerateMips).
//
// Coverage (alpha) fades out over the last VSLR_FADE part of the capture distance and with the
// weather's fog, so the far end of the capture blends into the sky reflection instead of
// ending at a hard edge. Where nothing was drawn the distance target holds its clear value
// (10 km, see the .cpp), which reads as sky.
//////////////////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "vslr_common.h"

// Part of the capture distance over which the capture fades into the sky reflection
#ifndef VSLR_FADE
#	define VSLR_FADE 0.3
#endif

TextureCube			s_vslr_color;	// linear colour
TextureCube<float>	s_vslr_dist;	// distance from the camera, metres

RWTexture2D<float4>	u_vslr_env : register( u0 );
RWTexture2D<float>	u_vslr_envdist : register( u1 );	// LEWD: distance panorama (localized lookup in pbr_ixray.h)

uniform float4		vslr_panorama_params;	// x = size, y = 1 / size, z = capture distance (m)

// Capture point of each cube face (+X, -X, +Y, -Y, +Z, -Z) relative to this frame's capture point
// (vslr_center). With r4_vslr_faces < 6 the faces were captured on different frames, from where the
// camera was then; their distances are re-measured from vslr_center so the localized lookup in
// pbr_ixray.h sees one consistent distance field while the camera moves.
uniform float4		vslr_face_offset[6];

// Distance of "nothing drawn" (r4_rendertarget_phase_vslr.cpp VSLR_SKY_DISTANCE): left as is
#define VSLR_SKY_DISTANCE 10000.0

uint vslr_cube_face( float3 d )
{
	float3 a = abs( d );
	if( a.x >= a.y && a.x >= a.z )	return d.x >= 0.0 ? 0 : 1;
	if( a.y >= a.z )				return d.y >= 0.0 ? 2 : 3;
	return d.z >= 0.0 ? 4 : 5;
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	const uint size = uint( vslr_panorama_params.x );
	if( id.x >= size || id.y >= size )
		return;

	float2 uv	= ( float2( id.xy ) + 0.5 ) * vslr_panorama_params.y;
	float2 p	= float2( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0 );
	float3 dir	= vslr_oct_decode( p );

	float3 col	= s_vslr_color.SampleLevel( smp_linear, dir, 0 ).rgb;
	float  dist	= s_vslr_dist.SampleLevel( smp_nofilter, dir, 0 );

	float far_dist	= max( vslr_panorama_params.z, 1.0 );
	float cover	= 1.0 - saturate( ( dist - far_dist * ( 1.0 - VSLR_FADE ) ) / ( far_dist * VSLR_FADE ) );
	float fog	= saturate( dist * fog_params.w + fog_params.x );
	cover		*= 1.0 - fog * fog;

	float3 g	= pow( max( col, 0.0 ), 1.0 / VSLR_GAMMA );
	u_vslr_env[ id.xy ] = float4( g * cover, cover );
	// Re-measured from this frame's capture point (radial part of the face's position offset)
	float dist_c	= dist >= VSLR_SKY_DISTANCE * 0.9 ? dist : length( vslr_face_offset[ vslr_cube_face( dir ) ].xyz + dir * dist );
	u_vslr_envdist[ id.xy ] = dist_c;
}
