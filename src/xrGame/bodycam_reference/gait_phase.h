#pragma once
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
inline float WrapFootPhase(float p)
{
	return p - std::floor(p);
}
// Foot phase (left contact at 0, right at .5) -> clip phase; left and right are the clip's
// contact phases from the actor's step_params.
inline float LegPhase(float foot, float left, float right)
{
	foot = WrapFootPhase(foot);
	left = WrapFootPhase(left);
	right = WrapFootPhase(right);
	const float span = WrapFootPhase(right - left);
	return foot < .5f ? WrapFootPhase(left + 2 * foot * span) : WrapFootPhase(right + 2 * (foot - .5f) * (1 - span));
}
// Inverse of LegPhase.
inline float FootPhase(float leg, float left, float right)
{
	leg = WrapFootPhase(leg);
	left = WrapFootPhase(left);
	right = WrapFootPhase(right);
	const float span = WrapFootPhase(right - left);
	if (span <= 0.f || span >= 1.f)
		return WrapFootPhase(leg - left);
	const float fromLeft = WrapFootPhase(leg - left);
	return fromLeft < span ? .5f * fromLeft / span : WrapFootPhase(.5f + .5f * (fromLeft - span) / (1 - span));
}
inline bool FootContactCrossed(float previous, float current, float contact)
{
	const float delta = WrapFootPhase(current - previous), distance = WrapFootPhase(contact - previous);
	// No catch-up bursts on discontinuous reseeks or very long stalls.
	return delta > 0 && delta <= .5f && distance > 0 && distance <= delta;
}
}
