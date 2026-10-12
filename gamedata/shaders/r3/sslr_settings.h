#ifndef SSLR_SETTINGS_H
#define SSLR_SETTINGS_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR (screen-space local reflections) settings
//
// Compile-time settings. Shaders recompile on game restart (delete the shader cache if you
// change them and nothing happens). Runtime settings are console commands:
//   r4_sslr_reflections 0/1   - on/off. Needs a GAME restart: renderer options are read once at
//                               startup (vid_restart only recreates render targets)
//   r4_sslr_intensity  0..1   - how strongly SSLR replaces the cubemap reflection
//   r4_sslr_temporal   0..0.98 - history weight of the temporal filter
//   r4_sslr_max_dist   1..300  - longest traced ray, metres
//   r4_sslr_skip_ssfx_ssr 0/1 - skip the Screen Space Shaders SSR pass entirely while SSLR
//                               is on (faster; HUD/weapon reflections then fall back to IBL)
//////////////////////////////////////////////////////////////////////////////////////////

// Linear ray-march steps (before the binary refinement). Cost scales linearly.
#ifndef SSLR_STEPS
#	define SSLR_STEPS 24
#endif

// Binary refinement steps after the first intersection
#ifndef SSLR_REFINE_STEPS
#	define SSLR_REFINE_STEPS 4
#endif

// Step distribution exponent: > 1 puts more samples near the start of the ray, where
// contact reflections need the precision
#ifndef SSLR_STEP_POW
#	define SSLR_STEP_POW 1.6
#endif

// Assumed thickness of depth-buffer surfaces, as a fraction of their depth plus a constant
// in metres. Too small: rays slip through thin objects. Too large: halos behind objects.
#ifndef SSLR_THICKNESS_REL
#	define SSLR_THICKNESS_REL 0.03
#endif
#ifndef SSLR_THICKNESS_ABS
#	define SSLR_THICKNESS_ABS 0.15
#endif

// Pixels closer than this (view-space metres) are treated as HUD (weapon, hands). Anomaly
// draws the HUD with its own projection, so screen-space rays from or onto it are wrong;
// they are skipped and keep the cubemap / SSS reflection.
// Caveat: world geometry closer than this (hugging a wall) is skipped too.
#ifndef SSLR_HUD_DEPTH
#	define SSLR_HUD_DEPTH 1.2
#endif

// Depth at or below which a G-buffer pixel is sky (Anomaly clears position to 0)
#ifndef SSLR_SKY_EPS
#	define SSLR_SKY_EPS 0.001
#endif

// Rougher than this: no ray is traced, the pixel keeps the cubemap reflection
#ifndef SSLR_ROUGHNESS_CUTOFF
#	define SSLR_ROUGHNESS_CUTOFF 0.7
#endif

// Fraction of the screen over which hits fade out at the edges
#ifndef SSLR_EDGE_FADE
#	define SSLR_EDGE_FADE 0.08
#endif

// VNDF sampling bias (IX-Ray: Jitter * float2(1.0, 0.7)). Lower y = fewer fireflies, less
// accurate rough lobes.
#ifndef SSLR_VNDF_BIAS
#	define SSLR_VNDF_BIAS 0.7
#endif

// Spatial resolve: samples and the radius (pixels) at roughness 1
#ifndef SSLR_FILTER_SAMPLES
#	define SSLR_FILTER_SAMPLES 12
#endif
#ifndef SSLR_FILTER_RADIUS
#	define SSLR_FILTER_RADIUS 24.0
#endif

// Legacy (non-PBR) pixels have no roughness in the G-buffer. Approximate Enhanced Shaders'
// calc_rough(): rough = lerp(HIGH, LOW, gloss)^2. Only used to shape the SSLR rays; the
// lighting itself still uses ES's real conversion in combine_1.
#ifndef SSLR_LEGACY_ROUGH_HIGH
#	define SSLR_LEGACY_ROUGH_HIGH 0.95
#endif
#ifndef SSLR_LEGACY_ROUGH_LOW
#	define SSLR_LEGACY_ROUGH_LOW 0.35
#endif

// HDR: the colour SSLR reads ($user$generic_temp) is combine_1's tonemapped 8-bit output.
// 1 = invert that tonemap (vanilla curve, or Enhanced Shaders' when the engine reports ES via
// MONOLITH_ES) and divide by the exposure, so reflected highlights keep their pre-tonemap
// brightness, like the cubemap reflection (hdr_ibl.h). 0 = use the tonemapped colour.
// Caveat: values the tonemap clipped are lost. Recoverable maximum, divided by the exposure:
// 1.7 for vanilla (gamma space), 11.2 linear (about 3.0 in gamma space) for Enhanced Shaders.
#ifndef SSLR_HDR_DECODE
#	define SSLR_HDR_DECODE 1
#endif
#ifndef SSLR_TM_WHITE_VANILLA
#	define SSLR_TM_WHITE_VANILLA 1.7		// common_functions.h tonemap(), fWhiteIntensity
#endif
#ifndef SSLR_TM_WHITE_ES
#	define SSLR_TM_WHITE_ES 11.2		// Enhanced Shaders tonemap(), fWhiteIntensity
#endif
// Upper limit of a decoded SSLR sample (gamma space), against fireflies in the resolve
#ifndef SSLR_HDR_MAX
#	define SSLR_HDR_MAX 8.0
#endif

// Temporal accumulation (sslr_temporal.cs)
// Variance clipping width in standard deviations. Lower = less ghosting, more noise/flicker.
#ifndef SSLR_TEMPORAL_GAMMA
#	define SSLR_TEMPORAL_GAMMA 1.25
#endif
// Surface motion (pixels per frame) at which the history weight is halved
#ifndef SSLR_TEMPORAL_MOTION_PX
#	define SSLR_TEMPORAL_MOTION_PX 24.0
#endif
// Roughness range over which the history switches from the reflection's virtual image (mirror)
// to the surface (rough). IX-Ray switches hard at 0.1.
#ifndef SSLR_VIRTUAL_ROUGH_LO
#	define SSLR_VIRTUAL_ROUGH_LO 0.08
#endif
#ifndef SSLR_VIRTUAL_ROUGH_HI
#	define SSLR_VIRTUAL_ROUGH_HI 0.25
#endif
// Previous-frame visibility test (trace colour fetch and temporal disocclusion): allowed
// depth difference = ABS metres + REL * depth. Too tight: holes and flicker on steep surfaces;
// too loose: ghosts come back.
#ifndef SSLR_REPROJ_DEPTH_TOL_ABS
#	define SSLR_REPROJ_DEPTH_TOL_ABS 0.1
#endif
#ifndef SSLR_REPROJ_DEPTH_TOL_REL
#	define SSLR_REPROJ_DEPTH_TOL_REL 0.04
#endif

// Debug output written into the reflection itself (combine shows it on every pixel that
// receives SSLR): 0 = off, 1 = hit confidence as grey, 2 = raw hit colour at full strength
#ifndef SSLR_DEBUG
#	define SSLR_DEBUG 0
#endif

// Screen Space Shaders interop (ssfx_ssr_combine.ps of the compatibility patch): with SSLR on
// and Enhanced Shaders' lighting patched to consume SSLR, SSS SSR is suppressed on non-HUD
// pixels so the two do not stack. 0 = always keep SSS SSR (reflections will double).
#ifndef SSLR_REPLACE_SSFX_SSR
#	define SSLR_REPLACE_SSFX_SSR 1
#endif

#endif	//	SSLR_SETTINGS_H
