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

#endif	//	PBR_SURFACE_H
