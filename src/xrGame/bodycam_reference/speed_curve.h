#pragma once
#include "speed_modifiers.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
// Maps engine movement speed onto Reference's gait speeds, anchored on the actor's current
// coefficients: engine run -> Reference walk, engine sprint -> Reference sprint, linear between,
// proportional below and above.
struct GaitSpeedAnchors
{
	double engineRun = 0, engineSprint = 0; // metres per second
	double referenceRun = 0, referenceSprint = 0;
	bool valid = false;
};

inline GaitSpeedAnchors GaitAnchors(const SpeedCoefficients& coefficients, SpeedState state, double referenceRun, double referenceSprint)
{
	GaitSpeedAnchors anchors;
	state.accelerated = true;
	state.sprinting = false;
	anchors.engineRun = SpeedBudget(coefficients, state) / double(kAccelerationPerSpeed);
	state.sprinting = true;
	anchors.engineSprint = SpeedBudget(coefficients, state) / double(kAccelerationPerSpeed);
	anchors.referenceRun = referenceRun;
	anchors.referenceSprint = referenceSprint;
	anchors.valid = std::isfinite(anchors.engineRun) && std::isfinite(anchors.engineSprint) && anchors.engineRun > .05 &&
		std::isfinite(referenceRun) && referenceRun > 0 && std::isfinite(referenceSprint);
	return anchors;
}

inline double ReferenceEquivalentSpeed(double speed, const GaitSpeedAnchors& a)
{
	speed = std::isfinite(speed) ? std::max(speed, 0.) : 0.;
	if (!a.valid)
		return speed;
	// Proportional up to run speed, and throughout when there is no distinct sprint (aiming,
	// crouching or equal budgets).
	if (speed <= a.engineRun || !(a.engineSprint > a.engineRun + .05) || !(a.referenceSprint > a.referenceRun))
		return a.referenceRun * speed / a.engineRun;
	if (speed <= a.engineSprint)
		return a.referenceRun + (speed - a.engineRun) / (a.engineSprint - a.engineRun) * (a.referenceSprint - a.referenceRun);
	return a.referenceSprint * speed / a.engineSprint;
}

// 0 at engine run speed, 1 at engine sprint speed.
inline double SprintProgress(double speed, const GaitSpeedAnchors& a)
{
	if (!a.valid || !(a.engineSprint > a.engineRun + .05) || !std::isfinite(speed))
		return 0;
	return std::clamp((speed - a.engineRun) / (a.engineSprint - a.engineRun), 0., 1.);
}
}
