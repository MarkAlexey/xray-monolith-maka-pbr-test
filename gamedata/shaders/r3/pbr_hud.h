#ifndef PBR_HUD_H
#define PBR_HUD_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray PBR materials for first-person (HUD) models under Screen Space Shaders
//
// With SSS installed the engine compiles every HUD model (weapons, hands, detectors) with
// SSS's own G-buffer shaders, deffer_base_hud_*.ps (ResourceManager.cpp: HudElement), not
// with deffer_base_bump.ps. Those shaders never wrote the PBR material target, so a PBR
// weapon was stored as a legacy pixel: its IX-Ray textures were then decoded the legacy way
// (the height channel read as gloss, the normal channels swapped) and, with unified lighting,
// converted by Enhanced Shaders' pseudo-PBR guess. Metals came out as matte white plastic.
//
// The compatibility patch (tools/make_pbr_compat_patch.py) makes SSS's HUD shaders call:
//
//     float4 pbr = pbr_hud_decode( S, I.tcdh.xy );	// after sload(), before the normal is used
//     ...
//     O.surface  = pbr_hud_surface( pbr );			// before return, only with PBR_GBUFFER
//
// Both compile to nothing unless the texture is PBR (USE_PBR, set by the engine per texture).
//
// Which sload.h is installed decides the decode:
//  - PBR-aware sload.h (this repository's, or the SSS 23.5.1 PBR compatibility patch's; they
//    define SLOAD_HEIGHT): S is already decoded IX-Ray style, parallax and detail included.
//    Its material is used as is.
//  - SSS's own sload.h: S was decoded the legacy way. The normal, height, gloss and the
//    material are re-read here from the IX-Ray texture layout (see sload.h):
//        bump  : x = height, y = normal Y, w = normal X (z unused)
//        bump# : x = metalness, y = roughness, z = subsurface, w = ambient occlusion
//    Caveats on this path: no parallax offset and no detail bump (HUD models rarely use
//    either); albedo detail from sload() is kept.
//
// The material target only exists with a PBR gbuffer_stage.h (PBR_GBUFFER). Under SSS that
// means the SSS 23.5.1 PBR compatibility patch must be installed; without it PBR_GBUFFER is
// undefined, nothing is written, and the weapon is lit as a legacy model, but with the
// correct normal and a gloss derived from the roughness.
//
// Rain drops on the weapon (SSS HUD Raindrops) still change the legacy gloss and the normal;
// they do not change the PBR roughness (same limitation as rain on PBR world surfaces).
//////////////////////////////////////////////////////////////////////////////////////////

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
