#pragma once
#include "locomotion_properties.h"

namespace BodycamReference
{
double LocomotionDeltaYaw(const Vector& velocity, Rotation reference);
int MovementDirectionFromAngle(int gait, int rotationMode, double angle);
struct LocomotionRotationState
{
	int movementDirection = 0;
	double fYaw = 0, bYaw = 0, lYaw = 0, rYaw = 0;
};
class GroundedRotationProperties
{
public:
	// Linear yaw curves for FB.X, FB.Y, LR.X, LR.Y.
	explicit GroundedRotationProperties(std::array<std::vector<LocomotionCurveKey>, 4> axes);
	const LocomotionRotationState& Update(bool movementValuesUpdate, const LocomotionEssential& essential);
	const LocomotionRotationState& State() const { return state_; }

private:
	std::array<LocomotionPropertyCurve, 4> axes_;
	LocomotionRotationState state_;
};
class MovingRifleWeight
{
public:
	double Update(int gait, double weightGait, double aimingProgress, double worldDelta);
	double Smooth() const { return smooth_; }
	double Weight() const { return weight_; }
	double Target() const { return target_; }
	double Curve() const { return curve_; }
	float GraphAlpha() const { return static_cast<float>(weight_); }

private:
	double smooth_ = 0, weight_ = 0, target_ = 0, curve_ = 0;
};
}
