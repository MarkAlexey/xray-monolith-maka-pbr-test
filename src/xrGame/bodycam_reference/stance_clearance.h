#pragma once
#include "constants.h"
#include "transform.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
// Viewmodel lift and retraction while crouched, in camera-local metres. The camera, collision
// and near-wall transform are not affected.
class StanceClearance
{
public:
	Vector Update(bool crouched, bool lowCrouched, double lookDownRadians, double engineAimBlend, double seconds)
	{
		if (!std::isfinite(lookDownRadians) || !std::isfinite(engineAimBlend) || !std::isfinite(seconds) || seconds < 0)
			throw std::invalid_argument("Stance clearance input is not finite or the time step is negative");
		const double down = std::clamp(lookDownRadians / (kPi * .5), 0., 1.);
		const bool low = crouched && lowCrouched;
		const double targetLift = crouched ? (low ? .06 + .04 * down : .03) : 0.;
		const double targetRetract = crouched ? (low ? .02 + .04 * down : .01) : 0.;
		const double response = -std::expm1(-std::min(seconds, .25) / .12);
		lift_ += (targetLift - lift_) * response;
		retract_ += (targetRetract - retract_) * response;
		// Fades with the engine's ADS blend, after smoothing, so full ADS keeps the sights on axis.
		const double hip = 1. - std::clamp(engineAimBlend, 0., 1.);
		return { 0, lift_ * hip, -retract_ * hip };
	}

private:
	double lift_ = 0, retract_ = 0;
};
}
