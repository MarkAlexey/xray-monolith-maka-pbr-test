#ifndef IXRAY_BRDF_H
#define IXRAY_BRDF_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: shared IX-Ray BRDF and X-Ray cubemap direction helpers
//
// One source of truth for:
//  - the IX-Ray metallic/roughness BRDF (GGX D, Smith G, Schlick F with the IX-Ray F90
//    trick, and the split-sum environment BRDF fit). Used by pbr_ixray.h (real PBR textures)
//    and by the patched Enhanced Shaders pbr_brdf.h (pseudo-PBR legacy textures), so both
//    material kinds are shaded by exactly the same math.
//  - remapping a world direction onto X-Ray's sky / hemisphere cubemaps.
//
// The BRDF math is IX-Ray's (gamedata/shaders/d3d11/metalic_roughness_*.hlsli,
// https://github.com/ixray-team/ixray-1.6-stcop). Credit to LVutner and the IX-Ray team.
//
// This file has no dependencies and declares no resources, so any shader can include it.
//////////////////////////////////////////////////////////////////////////////////////////

//========================================================================================
// Settings
//========================================================================================

// Sky cubemaps: clamp directions below the horizon to the bottom edge of the side faces,
// which is what the skybox itself shows there. 0 = let them run onto the bottom face
// (usually black or garbage in Anomaly skies).
#ifndef IXR_SKY_CLAMP_HORIZON
#	define IXR_SKY_CLAMP_HORIZON 1
#endif

// Hemisphere (env_s0/env_s1) cubemaps: same, for the hemisphere lighting cubemaps. Off by
// default: Enhanced Shaders' brightness calibration was done with the bottom face reachable,
// so clamping here makes below-horizon reflections slightly brighter than ES intended.
#ifndef IXR_ENV_CLAMP_HORIZON
#	define IXR_ENV_CLAMP_HORIZON 0
#endif

//========================================================================================
// Cubemap direction remap
//========================================================================================

// X-Ray sky and hemisphere cubemaps are authored for the skybox mesh, whose texture
// coordinates put the horizon on the bottom edge of the side faces: the visible upper half
// of the sky is stretched over the whole cube. Sampling them with a plain direction shows
// that stretch ("stretched mess"). Projecting onto the cube (divide by the largest
// component) and mapping y from [0, 1] to [-1, 1] undoes it.
//
// Based on Maka's unfuck():
//     float3 vabs = abs(temp); float vmax = max(vabs.x, max(vabs.y, vabs.z));
//     temp /= vmax; temp.y = temp.y * 2.0 - 1.0;
// plus a divide-by-zero guard and the optional horizon clamp. The result does not need to
// be normalised; cubemap sampling only uses the direction.
//
// Edge cases:
//  - dir == 0 (degenerate normal / reflection): returns (0, -1, 0) instead of NaN.
//  - Straight up (0, 1, 0) maps to itself; straight down maps to y = -3 unclamped, which the
//    sampler treats as the bottom face.
float3 ixr_cube_unfuck( float3 dir, bool clamp_horizon )
{
	float3 vabs	= abs( dir );
	float  vmax	= max( vabs.x, max( vabs.y, vabs.z ) );
	dir			/= max( vmax, 1e-6 );
	dir.y		= dir.y * 2.0 - 1.0;
	if( clamp_horizon )
		dir.y	= max( dir.y, -1.0 );
	return dir;
}

float3 ixr_cube_unfuck( float3 dir )
{
	return ixr_cube_unfuck( dir, false );
}

// Rotate a world direction around Y. cs = (cos, sin) of the angle.
float3 ixr_yaw( float3 w, float2 cs )
{
	return float3( w.x * cs.x - w.z * cs.y, w.y, w.x * cs.y + w.z * cs.x );
}

// World direction -> sky cubemap coordinates, matching how the engine draws the skybox
// (dxEnvironmentRender.cpp): rotated by the weather's sky_rotation, then unstretched.
// cs = (cos, sin) of the sky rotation; pass (1, 0) for none.
float3 ixr_sky_dir( float3 w, float2 cs )
{
	return ixr_cube_unfuck( ixr_yaw( w, cs ), IXR_SKY_CLAMP_HORIZON != 0 );
}

// World direction -> hemisphere cubemap coordinates (no rotation, see IXR_ENV_CLAMP_HORIZON)
float3 ixr_env_dir( float3 w )
{
	return ixr_cube_unfuck( w, IXR_ENV_CLAMP_HORIZON != 0 );
}

//========================================================================================
// IX-Ray BRDF (metalic_roughness_light.hlsli)
//
// Conventions: all dot products are between unit vectors pointing AWAY from the surface
// (to the light, to the eye), already clamped to >= 0 by the caller. roughness is
// perceptual roughness (IX-Ray squares it inside). No 1/PI anywhere: IX-Ray drops it from
// both the diffuse and the specular term, so the result is directly comparable to the
// legacy "albedo * NdotL" lighting.
//========================================================================================

float ixr_D_GGX( float NdotH, float roughness )
{
	float alpha		= roughness * roughness;
	float alpha2	= alpha * alpha + 1e-6;
	float divider	= NdotH * NdotH * ( alpha2 - 1.0 ) + 1.0;
	return alpha2 * rcp( divider * divider );
}

// Smith-Schlick visibility with k = (r + 1)^2 / 8, already divided by 4 * NdotL * NdotV
// except for the 0.25 that ixr_brdf() applies.
float ixr_G_Smith( float NdotL, float NdotV, float roughness )
{
	float r = roughness + 1.0;
	float k = r * r * 0.125;
	return rcp( lerp( NdotL, 1.0, k ) ) * rcp( lerp( NdotV, 1.0, k ) );
}

// IX-Ray F90: dielectrics (F0 ~ 0.04) reach 1 at grazing angles, very dark F0 values
// (< ~0.02 per channel average) are allowed to stay darker (specular occlusion trick).
float ixr_F90( float3 F0 )
{
	return min( 1.0, dot( F0, 16.66667 ) );
}

float3 ixr_F_Schlick( float3 F0, float cosine )
{
	return lerp( F0, ixr_F90( F0 ), pow( 1.0 - cosine, 5.0 ) );
}

// The full IX-Ray BRDF without NdotL: diffuse and specular are blended by Fresnel instead of
// added, which keeps the result energy-bounded for bright metals. diffuse is the linear
// diffuse colour (albedo * (1 - metalness)), F0 the linear specular colour.
float3 ixr_brdf( float3 diffuse, float3 F0, float roughness, float NdotL, float NdotH, float NdotV, float HdotV )
{
	float  D = ixr_D_GGX( NdotH, roughness );
	float  G = ixr_G_Smith( NdotL, NdotV, roughness );
	float3 F = ixr_F_Schlick( F0, HdotV );
	return lerp( diffuse, 0.25 * D * G, F );
}

// Convenience: vectors instead of dot products. N, L (to light) and V (to eye) unit length.
float3 ixr_brdf_vec( float3 diffuse, float3 F0, float roughness, float3 N, float3 L, float3 V )
{
	float3 H	= normalize( L + V );
	float NdotL	= saturate( dot( N, L ) );
	float NdotH	= saturate( dot( N, H ) );
	float NdotV	= saturate( dot( N, V ) );
	float HdotV	= saturate( dot( H, V ) );
	return ixr_brdf( diffuse, F0, roughness, NdotL, NdotH, NdotV, HdotV );
}

//========================================================================================
// IX-Ray ambient / IBL (metalic_roughness_ambient.hlsli)
//========================================================================================

// Split-sum environment BRDF, fitted for height-correlated Smith (IX-Ray)
float2 ixr_env_brdf_approx( float NdotV, float roughness )
{
	NdotV = min( NdotV, 0.998 );

	float nsqr = NdotV * NdotV;
	float rsqr = roughness * roughness;

	float4 fac = float4( 0.0187, 1.0133, 1.0000, 1.0000 ) +
	float4( 1.9496, -2.4717, -0.0333, 2.0508 ) * NdotV +
	float4( 1.2265, -1.2172, -1.3097, 0.2342 ) * roughness +
	float4( -7.6907, 3.4300, 0.5972, -26.9406 ) * NdotV * roughness +
	float4( 18.3314, 1.4794, 19.3537, 11.1429 ) * nsqr +
	float4( -0.2894, 0.5564, 1.5052, 7.0828 ) * rsqr +
	float4( -19.3056, -2.2456, -28.2302, 18.5470 ) * nsqr * roughness +
	float4( 7.0144, -1.8934, 1.3307, 50.6469 ) * NdotV * rsqr +
	float4( 1.5728, 1.3618, 15.2939, -63.3557 ) * nsqr * rsqr;

	return saturate( fac.xy / fac.zw );
}

// Environment Fresnel: how much of the ambient is reflection rather than diffuse
float3 ixr_env_F( float3 F0, float NdotV, float roughness )
{
	float2 brdf = ixr_env_brdf_approx( NdotV, roughness );
	return F0 * brdf.x + ixr_F90( F0 ) * brdf.y;
}

// IX-Ray AmbientLightingImpl: diffuse irradiance and specular radiance (both linear) blended
// by the environment Fresnel. Caller applies occlusion.
float3 ixr_ambient( float3 diffuse, float3 F0, float roughness, float NdotV, float3 irradiance, float3 radiance )
{
	return lerp( irradiance * diffuse, radiance, ixr_env_F( F0, NdotV, roughness ) );
}

#endif	//	IXRAY_BRDF_H
