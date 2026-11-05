#ifndef PBR_SURFACE_H
#define PBR_SURFACE_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: alpha channel of the PBR material target ($user$surface, RGBA8, gbuffer_stage.h)
//
//   0          legacy pixel (every non-PBR shader writes 0, see gbuffer_stage.h)
//   128..255   PBR pixel:  bit 0     = NO_MIRROR (per-texture .ltx option, see below)
//                          bits 1..6 = subsurface, 64 steps
//
// Readers that predate the flag decode the subsurface as saturate(w * 2 - 1) and the PBR
// test as w > 0.25; both still hold (the flag shifts the subsurface by under 1/100).
//
// NO_MIRROR: in the texture's .ltx, section [shaders_options]:
//     NO_MIRROR = 1
// The surface reflects only the sky (the cubemaps, blurred by its roughness), not its
// surroundings: no VSLR capture of nearby geometry, no SSLR. Metalness, roughness, the metal
// tint and every highlight are unchanged. Global switches for all PBR surfaces at once:
// r4_pbr_mirror_hud / r4_pbr_mirror_world (pbr_ixray.h). Restart the game after editing.
// Caveat: PBR textures only; a legacy texture has no material to carry the flag.
//
// Every writer of the material must use pbr_surface_w(): this repository's deffer_base_bump.ps
// / deffer_base_aref_bump.ps, and pbr_hud_surface() (pbr_hud.h) for the patched SSS/ES ones.
// A writer that still stores 0.5 + 0.5 * subsurface sets the flag at random on pixels with a
// subsurface value (skin); a shader pack built from an older repository can do that.
//////////////////////////////////////////////////////////////////////////////////////////

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

// 1 = the pixel reflects its surroundings (VSLR, SSLR), 0 = only the sky (NO_MIRROR).
// Legacy pixels (w = 0) always return 1.
float pbr_surface_mirror( float w )
{
	uint b = (uint)round( saturate( w ) * 255.0 );
	return ( w > 0.25 && ( b & 1u ) != 0u ) ? 0.0 : 1.0;
}

#endif	//	PBR_SURFACE_H
