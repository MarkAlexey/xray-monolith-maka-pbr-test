#ifndef SSLR_COMMON_H
#define SSLR_COMMON_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR, compute-shader side
//
// Port of IX-Ray's sslr_render / sslr_filter / sslr_temporal (LVutner, IX-Ray team) to the
// Anomaly / Monolith G-buffer. The main differences from IX-Ray:
//  - Anomaly's s_position holds xy = packed normal, z = LINEAR view depth, w = hemi/material
//    (IX-Ray's holds NDC depth in x). All depth tests compare linear view depth.
//  - No velocity buffer is required: hits and history are reprojected with m_previous
//    (previous view-projection * current inverse view), which the engine already computes
//    for SSS. Moving objects therefore ghost slightly in reflections.
//  - s_image is the previous frame's combine output ($user$generic_temp, gamma space, 8-bit),
//    the same buffer SSS SSR reads, so the result stays in gamma space (no LinearToGamma).
//  - No blue-noise texture array (Anomaly has none): interleaved gradient noise instead.
//  - Sky / environment fallback is NOT done here: a miss writes confidence 0 and the
//    consumer (combine_1) keeps its own cubemap reflection. That keeps the IBL calibration
//    of PBR and Enhanced Shaders untouched.
//  - IX-Ray's offscreen-cubemap fallback (VSLR) is not part of this port yet.
//
// Deliberately self-contained: it does not include common.h, so it compiles the same with
// vanilla, SSS and Enhanced Shaders installed. Constants are bound by name by the engine.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_settings.h"

#ifndef PI
#	define PI 3.14159265359
#endif

//========================================================================================
// Resources (names must match blender_sslr.cpp)
//========================================================================================

Texture2D		s_position;		// G-buffer: xy = normal, z = view depth, w = hemi/mtl
Texture2D		s_surface;		// G-buffer: PBR material (x metal, y rough, z AO, w flag)
Texture2D		s_diffuse;		// G-buffer: albedo, gloss
Texture2D		s_image;		// pass input (see each pass)
Texture2D		s_refl;			// pass input (see each pass)
Texture2D		s_tonemap;		// exposure (luminance adaptation), x = tonemap scale

SamplerState	smp_nofilter;
SamplerState	smp_rtlinear;

uniform float4x4	m_P;			// main camera projection
uniform float4x4	m_previous;		// current view space -> previous frame clip space
uniform float4		sslr_screen;	// x = width, y = height, z = 1/width, w = 1/height
uniform float4		sslr_params;	// x = intensity, y = temporal weight, z = max distance, w = frame index

//========================================================================================
// G-buffer helpers
//========================================================================================

// Same as gbuf_unpack_normal() in gbuffer_stage.h (non-INTERNAL_AMD packing, which is what
// Anomaly ships). Caveat: a gbuffer_stage.h built with INTERNAL_AMD would need the AMD
// version here too.
float3 sslr_unpack_normal( float2 norm )
{
	float2 theta;
	sincos( norm.x * 3.14159f, theta.x, theta.y );
	float2 phi = float2( sqrt( 1.0 - norm.y * norm.y ), norm.y );
	return float3( theta.y * phi.x, theta.x * phi.x, phi.y );
}

// Screen UV + linear view depth -> view-space position, using m_P directly so projection
// offsets (TAA jitter, if any) are honoured. Assumes a perspective projection
// (clip.w depends on z only), which is always the case for the main camera.
float3 sslr_view_pos( float2 uv, float z )
{
	float2 ndc	= ( uv - 0.5 ) * float2( 2.0, -2.0 );
	float  w	= m_P[3][2] * z + m_P[3][3];
	float  x	= ( ndc.x * w - m_P[0][2] * z - m_P[0][3] ) / m_P[0][0];
	float  y	= ( ndc.y * w - m_P[1][2] * z - m_P[1][3] ) / m_P[1][1];
	return float3( x, y, z );
}

float2 sslr_project( float3 P )
{
	float4 c = mul( m_P, float4( P, 1.0 ) );
	return c.xy * ( float2( 0.5, -0.5 ) / c.w ) + 0.5;
}

// Previous-frame UV of a current view-space position (static-scene reprojection)
float2 sslr_reproject( float3 P )
{
	float4 c = mul( m_previous, float4( P, 1.0 ) );
	return c.xy * ( float2( 0.5, -0.5 ) / max( c.w, 1e-5 ) ) + 0.5;
}

bool sslr_on_screen( float2 uv )
{
	return all( uv >= 0.0 ) && all( uv <= 1.0 );
}

// 1 in the middle of the screen, 0 at the edges
float sslr_edge_fade( float2 uv )
{
	float2 e = saturate( min( uv, 1.0 - uv ) / SSLR_EDGE_FADE );
	return e.x * e.y;
}

// Roughness of a G-buffer pixel: real for PBR textures, approximated for legacy ones
float sslr_roughness( float4 surface, float gloss )
{
	[branch] if( surface.w > 0.25 )
		return max( 0.02, surface.y );

	float r = lerp( SSLR_LEGACY_ROUGH_HIGH, SSLR_LEGACY_ROUGH_LOW, saturate( gloss ) );
	return max( 0.02, r * r );
}

// Inverse of combine_1's tonemap(): the 8-bit colour in $user$generic_temp back to the
// pre-tonemap gamma-space colour combine_1 computed (see SSLR_HDR_DECODE).
float3 sslr_untonemap( float3 low )
{
#if SSLR_HDR_DECODE
	float scale = max( s_tonemap.Load( int3( 0, 0, 0 ) ).x, 1e-4 );
	low = saturate( low );
#	ifdef MONOLITH_ES
	// ES: low = gamma( (x / (x + 1)) / (w / (w + 1)) ), x = linear(colour) * linear(scale)
	const float w = SSLR_TM_WHITE_ES;
	float3 y = min( pow( low, 2.2 ) * ( w / ( w + 1.0 ) ), 0.9999 );
	float3 x = y / ( 1.0 - y );
	float3 c = pow( x, 1.0 / 2.2 ) / scale;
#	else
	// vanilla: low = x (1 + x / W^2) / (1 + x), x = colour * scale; solve the quadratic
	const float a = 1.0 / ( SSLR_TM_WHITE_VANILLA * SSLR_TM_WHITE_VANILLA );
	float3 b = 1.0 - low;
	float3 x = ( -b + sqrt( b * b + 4.0 * a * low ) ) / ( 2.0 * a );
	float3 c = x / scale;
#	endif
	return min( c, SSLR_HDR_MAX );
#else
	return low;
#endif
}

bool sslr_is_sky( float z )	{ return z <= SSLR_SKY_EPS; }
bool sslr_is_hud( float z )	{ return z < SSLR_HUD_DEPTH; }

//========================================================================================
// Noise and sampling
//========================================================================================

// Interleaved gradient noise (Jimenez 2014), animated per frame
float sslr_ign( float2 pix, float frame )
{
	pix += 5.588238 * fmod( frame, 64.0 );
	return frac( 52.9829189 * frac( dot( pix, float2( 0.06711056, 0.00583715 ) ) ) );
}

float2 sslr_noise2( uint2 pix, float frame )
{
	return float2( sslr_ign( float2( pix ), frame ), sslr_ign( float2( pix ) + 17.0, frame + 31.0 ) );
}

// GGX visible-normal sampling (Heitz 2018). N unit normal, V unit vector TO the eye,
// u in [0, 1)^2, alpha = roughness^2. Returns a world/view-space half vector.
// SSLR_VNDF_BIAS < 1 trims the outer part of the lobe (fewer fireflies, IX-Ray does the same
// with a biased jitter).
float3 sslr_sample_vndf( float3 N, float3 V, float2 u, float alpha )
{
	float3 T = normalize( abs( N.y ) < 0.999 ? cross( float3( 0.0, 1.0, 0.0 ), N ) : cross( float3( 1.0, 0.0, 0.0 ), N ) );
	float3 B = cross( N, T );

	float3 Vt	= float3( dot( V, T ), dot( V, B ), dot( V, N ) );
	float3 Vh	= normalize( float3( alpha * Vt.x, alpha * Vt.y, max( Vt.z, 1e-4 ) ) );

	float lensq	= Vh.x * Vh.x + Vh.y * Vh.y;
	float3 T1	= lensq > 0.0 ? float3( -Vh.y, Vh.x, 0.0 ) * rsqrt( lensq ) : float3( 1.0, 0.0, 0.0 );
	float3 T2	= cross( Vh, T1 );

	float r		= sqrt( u.x * SSLR_VNDF_BIAS );
	float phi	= 2.0 * PI * u.y;
	float t1	= r * cos( phi );
	float t2	= r * sin( phi );
	float s		= 0.5 * ( 1.0 + Vh.z );
	t2			= ( 1.0 - s ) * sqrt( saturate( 1.0 - t1 * t1 ) ) + s * t2;

	float3 Nh	= t1 * T1 + t2 * T2 + sqrt( saturate( 1.0 - t1 * t1 - t2 * t2 ) ) * Vh;
	float3 Ht	= normalize( float3( alpha * Nh.x, alpha * Nh.y, max( 0.0, Nh.z ) ) );

	return Ht.x * T + Ht.y * B + Ht.z * N;
}

// IX-Ray GGX distribution (same as ixray_brdf.h ixr_D_GGX, copied to stay self-contained)
float sslr_D_GGX( float NdotH, float roughness )
{
	float alpha		= roughness * roughness;
	float alpha2	= alpha * alpha + 1e-6;
	float divider	= NdotH * NdotH * ( alpha2 - 1.0 ) + 1.0;
	return alpha2 * rcp( divider * divider );
}

#endif	//	SSLR_COMMON_H
