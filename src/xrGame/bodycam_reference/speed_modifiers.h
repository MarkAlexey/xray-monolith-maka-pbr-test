#pragma once
#include <cmath>
namespace BodycamReference
{
// CPHMovementControl::Calculate converts an acceleration magnitude to a speed by dividing by 10.
constexpr float kAccelerationPerSpeed = 10;
// The actor's movement speed coefficients; the values loaded from config are the baseline that
// script changes are measured against.
struct SpeedCoefficients
{
	float walk = 0, run = 1, runBack = 1, walkBack = 1, crouch = 1, sprint = 1;
	float walkStrafe = 1, runStrafe = 1, sprintStrafe = 1;
};
struct SpeedState
{
	bool accelerated = false, aiming = false, back = false, crouched = false, sprinting = false, strafe = false;
};
inline float SpeedBudget(const SpeedCoefficients& c, const SpeedState& s)
{
	float value = c.walk;
	if (s.accelerated && !s.aiming)
		value *= s.back ? c.runBack : c.run;
	else if (s.back)
		value *= c.walkBack;
	if (s.crouched)
		value *= c.crouch;
	if (s.sprinting)
		value *= c.sprint;
	if (s.strafe && !s.crouched)
		value *= s.sprinting ? c.sprintStrafe : s.accelerated ? c.runStrafe : c.walkStrafe;
	return value;
}
inline float ReferenceAcceleration(float sourceSpeed, float currentBudget, float loadedBudget)
{
	if (!std::isfinite(sourceSpeed) || !std::isfinite(currentBudget) || !std::isfinite(loadedBudget) || sourceSpeed < 0 ||
		currentBudget < 0 || loadedBudget <= 0)
		return 0;
	return kAccelerationPerSpeed * sourceSpeed * (currentBudget / loadedBudget);
}
}
