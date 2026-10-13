//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray VSLR capture (offscreen reflections), see r4_rendertarget_phase_vslr.cpp.
//
// Pixel shader of E[SE_R2_REFLECTIONS] (uber_reflect in uber_deffer.cpp). The geometry's own
// vertex shader runs unchanged (deffer_base_*, deffer_model_*, deffer_tree_*), with the cube
// face's view and projection, so positions and normals here are in that face's view space.
// Entry files: deffer_vslr[_lmh][_aref]_flat|_bump.ps define VSLR_FLAT / VSLR_AREF /
// USE_LM_HEMI and include this.
//
// Based on IX-Ray's forward_base.ps (LVutner / IX-Ray team), cut down for a small, blurred
// capture without a shadow map:
//   - sun: the baked sun visibility of lightmapped level geometry (lightmap hemi .g); dynamic
//     objects and non-lightmapped geometry use their hemi (sky visibility) as an estimate;
//   - sky: hemisphere colour x hemi, plus the weather's ambient colour;
//   - static lights: the level's lightmap colour (s_lmap, lightmapped geometry only);
//   - PBR textures: metalness / roughness from the IX-Ray layout (pbr_hud.h), so metals keep
//     their colour; legacy textures are treated as non-metals.
// Output: linear colour (R11G11B10F) and the distance from the camera (R16F).
//
// Settings: VSLR_LMAP_SCALE (static light strength), VSLR_SUN_HEMI (dynamic-object sun estimate).
//////////////////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "sload.h"
#ifndef VSLR_FLAT
#	include "pbr_hud.h"
#endif

// Strength of the baked lightmap colour (R1 lightmaps were applied as light * albedo * 2)
#ifndef VSLR_LMAP_SCALE
#	define VSLR_LMAP_SCALE 2.0
#endif

// Dynamic objects: sun visibility = smoothstep(x, y, hemi)
#ifndef VSLR_SUN_HEMI
#	define VSLR_SUN_HEMI float2( 0.2, 0.6 )
#endif

// s_lmap (lightmap colour) is declared in common_samplers.h; uber_reflect binds it for level
// geometry

struct vslr_out
{
	float4	color	: SV_Target0;	// linear radiance (alpha unused)
	float	dist	: SV_Target1;	// distance from the camera, metres
};

float3 vslr_to_linear( float3 c ) { return pow( max( c, 0.0 ), 2.2 ); }

#ifdef VSLR_FLAT
vslr_out main( p_flat I )
#else
vslr_out main( p_bumped I )
#endif
{
	vslr_out O;

	float	metal	= 0.0;
#ifdef VSLR_FLAT
	float4	base	= tbase( I.tcdh.xy );
	float3	Nv		= normalize( I.N.xyz );
#else
	surface_bumped S = sload( I );
	float4	base	= S.base;
#	ifdef USE_PBR
	float4	pbr		= pbr_hud_decode( S, I.tcdh.xy );
	metal			= pbr.x;
#	endif
	float3	Nv		= normalize( mul( float3x3( I.M1, I.M2, I.M3 ), S.normal ) );
#endif

#ifdef VSLR_AREF
	clip( base.a - def_aref );
#endif

	// Sky visibility, sun visibility, static light
	float3	lmap	= 0.0;
#ifdef USE_LM_HEMI
	float4	lm		= s_hemi.Sample( smp_rtlinear, I.lmh );
	float	hemi	= get_hemi( lm );
	float	sun		= get_sun( lm );
	lmap			= vslr_to_linear( s_lmap.Sample( smp_rtlinear, I.lmh ).rgb ) * VSLR_LMAP_SCALE;
#else
	float	hemi	= I.position.w;
	float	sun		= smoothstep( VSLR_SUN_HEMI.x, VSLR_SUN_HEMI.y, hemi );
#endif

	float3	Nw		= normalize( mul( m_inv_V, Nv ) );
	float3	albedo	= vslr_to_linear( base.rgb );
	float3	diffuse	= albedo * ( 1.0 - metal );
	float3	spec	= lerp( (float3)0.04, albedo, metal );

	// Sun: Lambert, metals reflect it with their own colour (the capture is too small and
	// blurred for a specular highlight to matter)
	float	NdotL	= saturate( dot( Nw, -L_sun_dir_w ) );
	float3	direct	= vslr_to_linear( L_sun_color.rgb ) * ( NdotL * sun ) * ( diffuse + spec * 0.5 );

	// Sky and ambient
	float3	sky		= vslr_to_linear( L_hemi_color.rgb ) * hemi * ( 0.5 + 0.5 * Nw.y ) + vslr_to_linear( L_ambient.rgb );
	float3	ambient	= sky * ( diffuse + spec );

	O.color	= float4( direct + ambient + lmap * diffuse, 1.0 );
	O.dist	= length( I.position.xyz );
	return O;
}
