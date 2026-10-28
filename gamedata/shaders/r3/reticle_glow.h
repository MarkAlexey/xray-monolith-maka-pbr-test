// The 3DSS reticle glow (r4_rendertarget_phase_3dss_reticle.cpp). $user$reticle_glow_source holds the lit reticle cores in scene
// luminance, each weighted by its atlas's glow strength; the glow spreads it in three Gaussian lobes.
#ifndef RETICLE_GLOW_H
#define RETICLE_GLOW_H
#include "common.h"

#define RETICLE_GLOW_MAX_RADIUS 48

// x, y = source texels per step along the blur (one of them 0), z = sigma in source texels
uniform float4 reticle_glow_blur;

// One Gaussian pass over texels of `source` from pixel `px`, normalised over the taps it takes
float3 reticle_glow_gauss(Texture2D source, int2 px, float2 step, float sigma)
{
	uint w, h;
	source.GetDimensions(w, h);
	const int radius = min(int(ceil(3.0 * sigma)), RETICLE_GLOW_MAX_RADIUS);
	const float k = -0.5 / max(sigma * sigma, 1e-4);
	float3 sum = 0;
	float total = 0;
	[loop] for (int i = -radius; i <= radius; ++i)
	{
		const int2 p = clamp(px + int2(step * i), int2(0, 0), int2(w, h) - 1);
		const float g = exp(k * i * i);
		sum += g * source.Load(int3(p, 0)).rgb;
		total += g;
	}
	return sum / total;
}

int2 reticle_glow_pixel(Texture2D target_sized, float2 tc)
{
	uint w, h;
	target_sized.GetDimensions(w, h);
	return int2(tc * float2(w, h));
}
#endif
