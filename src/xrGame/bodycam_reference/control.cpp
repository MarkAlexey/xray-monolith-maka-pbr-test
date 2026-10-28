// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "control.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr float kAimLookScale = float(.7);
constexpr float kPitchLimit = float(89.9);
constexpr float kYawLimit = float(359.999);
// ADS progress per second.
constexpr float kAdsSpeed = float(2.2999997);
// Controls closer than this many degrees count as unchanged.
constexpr float kControlTolerance = float(.001);
double ClampAxis(double angle)
{
	double a = std::fmod(angle, 360.0);
	if (a < 0)
		a += 360;
	return a;
}
double NormalizeAxis(double angle)
{
	double a = ClampAxis(angle);
	if (a > 180)
		a -= 360;
	return a;
}
double LimitAngle(double angle, float minimum, float maximum)
{
	const double half = ClampAxis(double(maximum) - minimum) * .5;
	const double center = ClampAxis(double(minimum) + half), delta = NormalizeAxis(angle - center);
	const double chosen = delta > half ? center + half : delta < -half ? center - half : angle;
	return ClampAxis(NormalizeAxis(chosen));
}
bool SameAngle(double current, double incoming)
{
	const double value = current - incoming;
	double a = value - std::trunc(value / 360) * 360;
	if (a < 0)
		a += 360;
	if (a > 180)
		a -= 360;
	return std::abs(a) <= kControlTolerance;
}
}
void MouseAccumulator::Add(double x, double y)
{
	if (!std::isfinite(x) || !std::isfinite(y))
		throw std::invalid_argument("Mouse input must be finite");
	frame_.raw[0] += x;
	frame_.raw[1] += y;
	// One event is one sample on both axes. The counters are bytes and wrap.
	for (auto& count : frame_.samples)
		count = static_cast<std::uint8_t>(count + 1);
}
MouseFrame MouseAccumulator::Consume()
{
	const auto result = frame_;
	frame_ = {};
	return result;
}
std::array<double, 2> ModifyRawLook(double x, double y, bool aiming)
{
	if (!std::isfinite(x) || !std::isfinite(y))
		throw std::invalid_argument("Mouse input must be finite");
	const float scale = aiming ? kAimLookScale : 1;
	const double lookX = float(double(float(x)) * scale), lookY = float(double(float(-y)) * scale);
	// The squared look below which the input counts as idle: one float step below 1e-8.
	const std::uint64_t thresholdBits = 0x3e45798ec0000000ull;
	double threshold;
	std::memcpy(&threshold, &thresholdBits, sizeof(threshold));
	const bool actuated = lookX * lookX + lookY * lookY >= threshold;
	return actuated ? std::array<double, 2>{ lookX, lookY } : std::array<double, 2>{};
}
Rotation ControllerDeltas(double lookX, double lookY)
{
	return { float(double(float(lookY)) * float(-kControlDegreesPerLook)), float(double(float(lookX)) * float(kControlDegreesPerLook)), 0 };
}
Rotation ApplyControlDeltas(Rotation current, const Rotation& delta)
{
	if (!std::isfinite(current.pitch) || !std::isfinite(current.yaw) || !std::isfinite(current.roll) || !std::isfinite(delta.pitch) ||
		!std::isfinite(delta.yaw) || !std::isfinite(delta.roll))
		throw std::invalid_argument("Control rotation and delta must be finite");
	Rotation incoming{ LimitAngle(current.pitch + delta.pitch, -kPitchLimit, kPitchLimit),
		LimitAngle(current.yaw + delta.yaw, 0, kYawLimit), LimitAngle(current.roll + delta.roll, -kPitchLimit, kPitchLimit) };
	if (SameAngle(current.pitch, incoming.pitch) && SameAngle(current.yaw, incoming.yaw) && SameAngle(current.roll, incoming.roll))
		return current;
	return incoming;
}
Rotation ApplyRawLook(Rotation current, double x, double y, bool aiming)
{
	const auto look = ModifyRawLook(x, y, aiming);
	return ApplyControlDeltas(current, ControllerDeltas(look[0], look[1]));
}
float AdvanceAds(float progress, bool aiming, double dt)
{
	const float step = float(double(float((aiming ? 1.0 : -1.0) * float(dt))) * kAdsSpeed);
	return std::clamp(float(double(progress) + step), 0.f, 1.f);
}
float AdsCurve(float progress)
{
	const float angle = float(double(float(double(progress) * float(kPi))) - float(kPi / 2));
	const float sine = float(std::sin(double(angle)));
	return std::clamp(float(double(float(double(sine) + 1)) * .5), 0.f, 1.f);
}
}
