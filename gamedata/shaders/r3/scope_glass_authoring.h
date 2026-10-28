// The optic's authored lens reflection look (3DSS s3ds_reflection_hue and lens
// brightness), as offsets from neutral so an optic that defines nothing gives
// zero: x = coating hue shift in turns, y = reflection brightness scale - 1.
#ifndef SCOPE_GLASS_AUTHORING_H
#define SCOPE_GLASS_AUTHORING_H
uniform float4 svp_lens_authoring;
// The player's PiP lens look adjustments (r__svp_lens_*), as offsets from neutral so that zero,
// and an unbound constant, leave the lens unchanged. svp_lens_adjust: x coating hue shift in
// turns, y coating saturation - 1, z reflection blur (-1 sharper .. 1 rough), w dirt scale - 1.
// svp_lens_adjust_light: x environment reflection scale - 1, y glint scale - 1.
uniform float4 svp_lens_adjust;
uniform float4 svp_lens_adjust_light;

// Rotates a colour about the grey axis by a fraction of the hue circle.
float3 svp_optic_rotate_hue(float3 colour, float turns)
{
	float angle = turns * 6.28318531;
	float cosine = cos(angle);
	const float3 grey = float3(0.57735027, 0.57735027, 0.57735027);
	return colour * cosine + cross(grey, colour) * sin(angle)
		+ grey * dot(grey, colour) * (1.0 - cosine);
}

// The coating hue with the optic's authored hue shift, then the player's hue and saturation.
float3 svp_lens_authored_hue(float3 coating)
{
	if (svp_lens_authoring.x != 0.0)
		coating = max(svp_optic_rotate_hue(coating, svp_lens_authoring.x), 0.0);
	if (svp_lens_adjust.x != 0.0)
		coating = max(svp_optic_rotate_hue(coating, svp_lens_adjust.x), 0.0);
	if (svp_lens_adjust.y != 0.0)
	{
		float luma = dot(coating, float3(0.2126, 0.7152, 0.0722));
		coating = max(lerp(luma.xxx, coating, max(1.0 + svp_lens_adjust.y, 0.0)), 0.0);
	}
	return coating;
}

// Roughness of the environment reflection lookup with the player's reflection blur.
float svp_lens_blur_roughness(float roughness)
{
	float blur = svp_lens_adjust.z;
	if (blur == 0.0)
		return roughness;
	roughness = saturate(roughness);
	return blur > 0.0 ? lerp(roughness, 1.0, saturate(blur)) : roughness * (1.0 + max(blur, -1.0));
}

// Scales the glass dirt. Dirt raises roughness and lowers metallic together (source17: x dirt
// limit, y metallic lost to full dirt; source18: x dirt exponent, y roughness span, z clean
// roughness), so both are rebuilt from the scaled dirt amount.
void svp_lens_adjust_dirt(inout float roughness, inout float metallic, float4 source17, float4 source18)
{
	if (svp_lens_adjust.w == 0.0 || source18.x <= 1.e-5 || source18.y <= 1.e-5)
		return;
	float dirt = pow(saturate((saturate(roughness) - source18.z) / source18.y), 1.0 / source18.x);
	float scaled = saturate(dirt * max(1.0 + svp_lens_adjust.w, 0.0));
	float limit = max(source17.x, 0.0);
	metallic = saturate(metallic - (min(scaled, limit) - min(dirt, limit)) * source17.y);
	roughness = saturate(source18.z + source18.y * pow(scaled, source18.x));
}
float svp_lens_dirt_roughness(float roughness, float4 source17, float4 source18)
{
	float metallic = 0.0;
	svp_lens_adjust_dirt(roughness, metallic, source17, source18);
	return roughness;
}
float svp_lens_environment_scale() { return max(1.0 + svp_lens_adjust_light.x, 0.0); }
float svp_lens_glint_scale() { return max(1.0 + svp_lens_adjust_light.y, 0.0); }

// The reflectance of a coating with the optic's authored hue and brightness.
float3 svp_lens_authored_reflectance(float3 reflectance)
{
	return svp_lens_authored_hue(reflectance) * max(1.0 + svp_lens_authoring.y, 0.0);
}
#endif
