#include "locomotion_rotation.h"
#include "constants.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
LocomotionPropertyCurve YawAxis(std::vector<LocomotionCurveKey> keys)
{
	if (keys.size() < 2 || keys.front().time != -180 || keys.back().time != 180 ||
		!std::all_of(keys.begin(), keys.end(), [](const auto& k) { return k.linear; }))
		throw std::invalid_argument("Yaw curve must be linear and span -180 to 180");
	// Only normalized [-180,180] is consumed; extrapolation never participates.
	return LocomotionPropertyCurve(std::move(keys));
}
}
double LocomotionDeltaYaw(const Vector& v, Rotation reference)
{
	const double yaw = (v[1] == 0 && v[0] == 0 ? 0 : std::atan2(v[1], v[0])) * kDegreesPerRadian;
	return static_cast<float>(WrapDegrees(yaw - reference.yaw));
}
int MovementDirectionFromAngle(int gait, int rotation, double angle)
{
	if (gait == 2 || rotation == 0)
		return 0;
	const double a = static_cast<float>(angle);
	if (a >= -75 && a <= 75)
		return 0;
	if (a >= 65 && a <= 115)
		return 1;
	if (a >= -115 && a <= -65)
		return 2;
	return 3;
}
GroundedRotationProperties::GroundedRotationProperties(std::array<std::vector<LocomotionCurveKey>, 4> axes)
	: axes_{ YawAxis(std::move(axes[0])), YawAxis(std::move(axes[1])), YawAxis(std::move(axes[2])), YawAxis(std::move(axes[3])) }
{
}
const LocomotionRotationState& GroundedRotationProperties::Update(bool eligible, const LocomotionEssential& e)
{
	if (!eligible)
		return state_;
	state_.movementDirection = MovementDirectionFromAngle(e.gait, e.rotationMode, LocomotionDeltaYaw(e.velocity, e.aimingRotation));
	const double yaw = LocomotionDeltaYaw(e.velocity, e.currentControlRotation);
	state_.fYaw = axes_[0].Sample(yaw);
	state_.bYaw = axes_[1].Sample(yaw);
	state_.lYaw = axes_[2].Sample(yaw);
	state_.rYaw = axes_[3].Sample(yaw);
	return state_;
}
double MovingRifleWeight::Update(int gait, double weightGait, double aimingProgress, double worldDelta)
{
	curve_ = static_cast<float>(weightGait);
	const double mapped = MapRangeClamped(curve_, 1.3, 2, 0, .7);
	target_ = gait == 1 ? mapped : 0;
	smooth_ = FInterpTo(smooth_, target_, worldDelta, 10);
	weight_ = smooth_ * (1 - aimingProgress);
	return weight_;
}
}
