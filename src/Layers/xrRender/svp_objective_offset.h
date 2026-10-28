#pragma once

#include <cmath>
#include <cstring>

// Source label the optic profile gives an objective offset that was measured
// from the scope mesh instead of read from a config.
constexpr const char* SVP_DETECTED_OFFSET_SOURCE = "engine_detection";

// True when the profile says its objective offset came from mesh detection.
// Provenance is read from the profile's source label and never inferred from
// the values, so an authored offset is kept even when it equals the measured
// one. The untyped console route carries no source and is never treated as
// detected.
inline bool svp_offset_is_detected(bool typed_route, bool has_offset, const char* source)
{
	return typed_route && has_offset && source && std::strcmp(source, SVP_DETECTED_OFFSET_SOURCE) == 0;
}

// A measured offset only holds around the eyepiece it was measured on. It is
// misplaced when that eyepiece lies more than one eyepiece radius from the one
// being rendered (a clip-on, or a second lens on the same mesh). Invalid input,
// such as a garbage pose on a weapon switch frame, gives no verdict.
inline bool svp_detected_eyepiece_misplaced(float eye_distance, float eyepiece_radius)
{
	if (!(eyepiece_radius > 1e-5f))
		return false;
	const float radii = eye_distance / eyepiece_radius;
	return std::isfinite(radii) && radii > 1.f;
}
