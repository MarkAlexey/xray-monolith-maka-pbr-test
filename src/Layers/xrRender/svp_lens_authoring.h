#pragma once

#include <cmath>

// 3DSS gives each optic a lens reflection hue (s3ds_param_2.x, 0..1) and a
// lens reflection brightness (s3ds_param_2.y). Its defaults, hue 0.65 and
// brightness 1.25, are what an optic gets when it defines nothing, and the
// coated lens model is calibrated to look right at them, so they map to no
// change. A zero or invalid brightness means no 3DSS values were pushed.
struct SvpLensAuthoring
{
	float hue_shift = 0.f;        // turns of the hue circle, in [-0.5, 0.5)
	float reflection_scale = 1.f; // multiplies the reflected energy
};

constexpr float SVP_3DSS_DEFAULT_HUE = 0.65f;
constexpr float SVP_3DSS_DEFAULT_BRIGHTNESS = 1.25f;
constexpr float SVP_LENS_MAX_REFLECTION_SCALE = 4.f;
// 3DSS 5.x no longer authors a brightness: it sends the 1.25 default with the thermal battery
// charge packed into the thousandths, 1.25 + (charge + 1) * 0.001, so that band is the default.
constexpr float SVP_3DSS_PACKED_CHARGE_BAND = 0.002f;

inline SvpLensAuthoring svp_lens_authoring(float hue, float brightness)
{
	// Values printed and parsed back by scripts differ from the defaults by a
	// few ulps; anything this close is the default.
	const float snap = 1e-4f;
	SvpLensAuthoring result;
	if (!std::isfinite(brightness) || brightness <= 0.f)
		return result;
	if (brightness >= SVP_3DSS_DEFAULT_BRIGHTNESS - snap
		&& brightness <= SVP_3DSS_DEFAULT_BRIGHTNESS + SVP_3DSS_PACKED_CHARGE_BAND + snap)
		brightness = SVP_3DSS_DEFAULT_BRIGHTNESS;
	const float scale = std::fmin(brightness / SVP_3DSS_DEFAULT_BRIGHTNESS, SVP_LENS_MAX_REFLECTION_SCALE);
	if (std::fabs(scale - 1.f) > snap)
		result.reflection_scale = scale;
	if (std::isfinite(hue))
	{
		float shift = hue - SVP_3DSS_DEFAULT_HUE;
		shift -= std::floor(shift + 0.5f);
		if (std::fabs(shift) > snap)
			result.hue_shift = shift;
	}
	return result;
}
