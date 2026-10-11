#ifndef HDR_IBL_H
#define HDR_IBL_H

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: HDR image-based reflections from LDR cubemaps
//
// Anomaly's sky and hemisphere cubemaps are 8-bit: the bright sky between branches can
// never be brighter than "white", so reflections of it look flat and grey (an LDR
// reflection). In a real HDR environment map those openings are many times brighter than
// the rest, and the reflection shows hot spots that the tonemapper and bloom then shape.
//
// The original HDR range is gone, so it is reconstructed: texel luminance above a knee is
// pushed up with a bounded inverse-Reinhard curve, so a fully white texel becomes PEAK times
// brighter and everything below the knee is untouched. Hue is kept (the colour is scaled by
// the luminance ratio). The result is pre-tonemap radiance; combine_1's tonemap() compresses
// it and its "high" output feeds bloom, so no other pipeline change is needed.
//
// Used by pbr_ixray.h (PBR and unified pixels) and by the compatibility patch's Enhanced
// Shaders hmodel.h. Self-contained, no resources.
//
// Known limits:
//  - It is a guess about the lost range, not real HDR data. Skies whose clouds are clipped to
//    white get bright clouds too; tune the knee/peak per taste, or set HDR_IBL_PEAK 1 to
//    disable.
//  - It is applied AFTER mip filtering, so rough (blurred) reflections get less of a boost
//    than a true HDR map would give them (expand(blur) < blur(expand)).
//////////////////////////////////////////////////////////////////////////////////////////

// 1 = reconstruct HDR highlights, 0 = off (reflections exactly as before).
// (A separate integer switch: the preprocessor cannot compare float literals like HDR_IBL_PEAK.)
#ifndef HDR_IBL_ENABLE
#	define HDR_IBL_ENABLE 1
#endif

// Linear luminance a fully white cubemap texel expands to (> 1).
#ifndef HDR_IBL_PEAK
#	define HDR_IBL_PEAK 6.0
#endif

// Linear luminance where the expansion starts (0..1). Below it nothing changes, so
// mid-tones and the overall reflection brightness stay as they were.
#ifndef HDR_IBL_KNEE
#	define HDR_IBL_KNEE 0.6
#endif

float hdr_ibl_luminance( float3 c ) { return dot( c, float3( 0.2126, 0.7152, 0.0722 ) ); }

// Linear LDR colour -> linear HDR colour
float3 hdr_ibl_expand_linear( float3 c )
{
#if HDR_IBL_ENABLE
	const float k	= HDR_IBL_KNEE;
	const float P	= ( HDR_IBL_PEAK - k ) / max( 1.0 - k, 1e-3 );	// peak of the segment above the knee

	float L			= hdr_ibl_luminance( c );
	float t			= saturate( ( L - k ) / max( 1.0 - k, 1e-3 ) );
	// Bounded inverse Reinhard on the segment: t' = t / (1 - t + t / P); 0 -> 0, 1 -> P,
	// slope 1 at the knee so there is no visible kink
	float t2		= t / max( 1.0 - t + t / P, 1e-4 );
	float Lh		= L <= k ? L : k + ( 1.0 - k ) * t2;
	return c * ( Lh / max( L, 1e-5 ) );
#else
	return c;
#endif
}

// Gamma-space LDR colour (as sampled from the cubemaps) -> gamma-space HDR colour, for code
// that works in gamma space and linearises later (legacy hmodel, pbr_specular_irradiance)
float3 hdr_ibl_expand_gamma( float3 g )
{
#if HDR_IBL_ENABLE
	float3 lin = pow( max( g, 0.0 ), 2.2 );
	return pow( hdr_ibl_expand_linear( lin ), 1.0 / 2.2 );
#else
	return g;
#endif
}

#endif	//	HDR_IBL_H
