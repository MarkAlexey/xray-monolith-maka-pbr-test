// The active weather cubes (env_s0/env_s1) as the lens glass reads them, forward and deferred.
// Two cube conventions exist. Sky-only cubes (stock and Melancholy weathers: small, one mip) hold
// the sky hemisphere spread over the whole cube and are read through the stock hmodel remap;
// they hold nothing below the horizon, so a reflection pointing down reads the horizon in its
// azimuth. Full-sphere cubes (Atmospherics: mipped panoramas with ground) are read as they are.
// r__svp_lens_env_convention picks the convention: 0 auto (one mip or at most 64 texels wide
// is sky-only), 1 sky-only, 2 full sphere. Underground (ssfx_is_underground) the weather cubes
// still show the outdoor sky, so the lens reflects none of it there, as Atmospherics' own screen
// space reflections do.
#ifndef SVP_LENS_ENVIRONMENT_H
#define SVP_LENS_ENVIRONMENT_H

#ifndef SVP_LENS_ENV_TEXTURES_DECLARED
#define SVP_LENS_ENV_TEXTURES_DECLARED
TextureCube s_lens_env0;
TextureCube s_lens_env1;
#endif
uniform float4 svp_lens_env_convention; // x: 0 auto, 1 sky-only, 2 full sphere; y: underground

bool svp_lens_cube_sky_only(uint width, uint levels)
{
	if (svp_lens_env_convention.x > 1.5)
		return false;
	if (svp_lens_env_convention.x > .5)
		return true;
	return levels <= 1u || width <= 64u;
}

// stock hmodel remap of a direction at or above the horizon
float3 svp_lens_sky_remap(float3 direction)
{
	direction /= max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
	if (direction.y < .999)
		direction.y = direction.y * 2 - 1;
	return normalize(direction);
}

float3 svp_lens_cube_sample(TextureCube cube, float3 direction, float mip)
{
	uint width, height, levels;
	cube.GetDimensions(0, width, height, levels);
	direction = normalize(direction);
	const float lod = min(mip, float(max(levels, 1u) - 1u));
	if (!svp_lens_cube_sky_only(width, levels))
		return cube.SampleLevel(smp_base, direction, lod).rgb;
	if (direction.y >= 0)
		return cube.SampleLevel(smp_base, svp_lens_sky_remap(direction), lod).rgb;
	// below the horizon: the horizon in the same azimuth; straight down has no azimuth, so it
	// fades to the mean of the four horizon directions
	const float azimuth = length(direction.xz);
	float3 horizon = 0;
	if (azimuth > 1e-4)
		horizon = cube.SampleLevel(smp_base, svp_lens_sky_remap(float3(direction.x, 0, direction.z) / azimuth), lod).rgb;
	const float3 mean = .25 * (cube.SampleLevel(smp_base, svp_lens_sky_remap(float3(1, 0, 0)), lod).rgb
		+ cube.SampleLevel(smp_base, svp_lens_sky_remap(float3(-1, 0, 0)), lod).rgb
		+ cube.SampleLevel(smp_base, svp_lens_sky_remap(float3(0, 0, 1)), lod).rgb
		+ cube.SampleLevel(smp_base, svp_lens_sky_remap(float3(0, 0, -1)), lod).rgb);
	return lerp(mean, horizon, smoothstep(0, .05, azimuth));
}

// the active weather pair, sRGB-encoded as stored; blend = descriptor weight
float3 svp_lens_weather(float3 direction, float mip, float blend)
{
	return lerp(svp_lens_cube_sample(s_lens_env0, direction, mip),
		svp_lens_cube_sample(s_lens_env1, direction, mip), saturate(blend))
		* (1 - saturate(svp_lens_env_convention.y));
}

#endif
