#pragma once
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
// Clamps a world frame delta in seconds to [.0005, .4] in single precision.
inline float ClampFrameDelta(double seconds)
{
	return std::max(float(.0005), std::min(float(.4), float(seconds)));
}
// Wraps an angle in degrees to (-180, 180].
inline double WrapDegrees(double value)
{
	double angle = value - std::trunc(value / 360) * 360;
	if (!(angle >= 0))
		angle += 360;
	if (angle > 180)
		angle -= 360;
	return angle;
}
// Maps [inLow, inHigh] linearly onto [outLow, outHigh] and clamps outside the input range.
inline double MapRangeClamped(double value, double inLow, double inHigh, double outLow, double outHigh)
{
	return outLow + (outHigh - outLow) * std::clamp((value - inLow) / (inHigh - inLow), 0., 1.);
}
// Moves current towards target by the fraction speed * dt and snaps once they are within 1e-4.
inline double FInterpTo(double current, double target, double dt, double speed)
{
	if (speed <= 0)
		return target;
	const double delta = target - current;
	return delta * delta < float(1e-8) ? target : current + delta * std::clamp(speed * dt, 0., 1.);
}
}
