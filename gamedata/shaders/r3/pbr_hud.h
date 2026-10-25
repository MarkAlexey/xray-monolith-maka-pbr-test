#ifndef PBR_HUD_H
#define PBR_HUD_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray PBR materials under Screen Space Shaders / Enhanced Shaders
//
// Used by SSS's HUD G-buffer shaders and, since the world PBR fix, by SSS's / ES's
// deffer_base_bump.ps and deffer_base_aref_bump.ps too (they replace this repository's
// versions, which write the material themselves). The name stays for compatibility.
//
// With SSS installed the engine compiles every HUD model (weapons, hands, detectors) with
// SSS's own G-buffer shaders, deffer_base_hud_*.ps (ResourceManager.cpp: HudElement), not
// with deffer_base_bump.ps. Those shaders never wrote the PBR material target, so a PBR
// weapon was stored as a legacy pixel: its IX-Ray textures were then decoded the legacy way
// (the height channel read as gloss, the normal channels swapped) and, with unified lighting,
// converted by Enhanced Shaders' pseudo-PBR guess. Metals came out as matte white plastic.
//
// The compatibility patch (tools/make_pbr_compat_patch.py) makes those shaders call:
//
//     float4 pbr = pbr_hud_decode( S, I.tcdh.xy );	// after sload(), before the normal is used
//     ...
//     O.surface  = pbr_hud_surface( pbr );			// before return, only with PBR_GBUFFER
//
// Both compile to nothing unless the texture is PBR (USE_PBR, set by the engine per texture).
//
// Which sload.h is installed decides the decode:
//  - PBR-aware sload.h (this repository's; it defines SLOAD_HEIGHT): S is already decoded IX-Ray style, parallax and detail included.
//    Its material is used as is.
//  - SSS's own sload.h: S was decoded the legacy way. The normal, height, gloss and the
//    material are re-read here from the IX-Ray texture layout (see sload.h):
//        bump  : x = height, y = normal Y, w = normal X (z unused)
//        bump# : x = metalness, y = roughness, z = subsurface, w = ambient occlusion
//    Caveats on this path: no parallax offset and no detail bump on PBR textures (HUD models
//    rarely use either; on world PBR surfaces it means no POM); albedo detail is kept.
//
// The material target only exists with a PBR gbuffer_stage.h (PBR_GBUFFER), which the same
// compatibility patch generates from SSS's. Without it nothing is written and the model is
// lit as a legacy model, but with the correct normal and a gloss derived from the roughness.
// The material is only read if the lighting shaders are PBR-aware: the compatibility patch
// also adds the PBR path to SSS's / ES's accum_*.ps and combine_1.ps.
//
// Rain drops on the weapon (SSS HUD Raindrops) still change the legacy gloss and the normal;
// they do not change the PBR roughness (same limitation as rain on PBR world surfaces).
//////////////////////////////////////////////////////////////////////////////////////////

// Per-texture reflection scale, for skin, cloth or leather that reflects too much. In the
// texture's .ltx (next to its THM, or an LTX-only texture), section [shaders_options]:
//     HUD_REFLECT   = 0.25    first-person models (hands, weapons): SSS's HUD shaders
//     WORLD_REFLECT = 0.25    everything else that uses this repository's / SSS's / ES's
//                             deffer_base_bump / deffer_base_aref_bump
// 1 = unchanged, 0 = no reflection left. Legacy textures: gloss * k. PBR textures: roughness
// moves towards 1 (lerp(1, rough, k)), so highlights spread out and dim instead of vanishing.
// The value is part of the shader's name (each value is its own cache entry): restart the
// game after editing the .ltx, no need to delete the shader cache.
// Caveats: HUD_REFLECT needs SSS (without it first-person models use the world shaders and
// read WORLD_REFLECT); terrain, detail objects and the flat (no bump) shaders read neither.
#if defined(HUD_REFLECT) || defined(WORLD_REFLECT)
float pbr_reflect_gloss( float gloss, float k )	{ return gloss * saturate( k ); }
float pbr_reflect_rough( float rough, float k )	{ return lerp( 1.0, rough, saturate( k ) ); }
#endif

#ifdef USE_PBR

// Returns the material: x = metalness, y = roughness, z = AO, w = subsurface.
// Fixes S.normal / S.height / S.gloss in place when sload() decoded them the legacy way.
float4 pbr_hud_decode( inout surface_bumped S, float2 tc )
{
#ifdef SLOAD_HEIGHT
	return S.pbr;	// PBR-aware sload.h: already decoded
#else
	float4 Nu	= s_bump.Sample( smp_base, tc );
	float4 NuE	= s_bumpX.Sample( smp_base, tc );

	float3 N;
	N.xy		= Nu.wy * 2.0 - 1.0;
	N.z			= sqrt( 1.0 - saturate( dot( N.xy, N.xy ) ) );
#	ifndef USE_DX_NORMAL_MAP
	N.y			= -N.y;	// IX-Ray normal maps are OpenGL style (green up), see sload.h
#	endif
	S.normal	= N;
	S.height	= Nu.x;

	float4 pbr	= saturate( float4( NuE.x, NuE.y, NuE.w, NuE.z ) );
	pbr.y		= max( 0.02, pbr.y );

	// Legacy-style gloss for passes that still read it (SSS rain, SSR on the HUD)
	S.gloss		= ( 1.0 - pbr.y ) * ( 1.0 - pbr.y );
	return pbr;
#endif
}

#ifdef PBR_GBUFFER
// The value deffer_base_bump.ps writes to the material target for PBR pixels
float4 pbr_hud_surface( float4 pbr )
{
	return float4( pbr.x, pbr.y, pbr.z, 0.5 + 0.5 * pbr.w );
}
#endif

#endif	//	USE_PBR

#endif	//	PBR_HUD_H
