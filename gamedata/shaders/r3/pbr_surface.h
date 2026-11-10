#ifndef PBR_SURFACE_H
#define PBR_SURFACE_H

// Alpha of the PBR material target: 0 = legacy pixel; 128..255 = PBR pixel with bit 0 =
// NO_MIRROR and bits 1..6 = subsurface (64 steps). Older readers (saturate(w * 2 - 1), w > 0.25)
// still decode it. NO_MIRROR = 1 in a texture's [shaders_options]: it reflects only the sky,
// no VSLR / SSLR. Every material writer must use pbr_surface_w().

float pbr_surface_w( float sss )
{
#ifdef NO_MIRROR
	const float flag = 1.0;
#else
	const float flag = 0.0;
#endif
	// Exact multiples of 1/255, so the UNORM write keeps bit 0 as written
	return ( 128.0 + 2.0 * round( saturate( sss ) * 63.0 ) + flag ) / 255.0;
}

// 1 = reflects its surroundings, 0 = only the sky (NO_MIRROR); legacy pixels return 1
float pbr_surface_mirror( float w )
{
	uint b = (uint)round( saturate( w ) * 255.0 );
	return ( w > 0.25 && ( b & 1u ) != 0u ) ? 0.0 : 1.0;
}

// Atmospherics PBR packing (LTX "material = 4.5", PBR_ATMOS), decoded exactly as Atmospherics'
// own sload.h does, so a texture looks the same with or without the PBR shaders.
// Shared by sload.h and pbr_hud.h (the decode under SSS's / ES's / Atmospherics' sload.h).
//   bump  : r = normal X, g = normal Y (used as stored: DirectX style), b = height, a unused
//   bump# : r = ambient occlusion, g = roughness, b = metalness, a unused
// Neither texture needs alpha, so both can be BC1 (BC7 for quality).
float3 pbr_atmos_normal( float4 bump )
{
	float2 n = bump.xy * 2.0 - 1.0;
	return float3( n, sqrt( saturate( 1.0 - dot( n, n ) ) ) );
}

// x = metalness, y = roughness, z = ambient occlusion, w = subsurface.
// The packing has no subsurface channel: SKIN_SSS = 0..1 in the texture's .ltx
// [shaders_options] sets it for the whole texture (default 0).
float4 pbr_atmos_material( float4 bumpX )
{
#ifdef SKIN_SSS
	const float sss = saturate( SKIN_SSS );
#else
	const float sss = 0.0;
#endif
	return float4( bumpX.z, bumpX.y, bumpX.x, sss );
}

#endif	//	PBR_SURFACE_H
