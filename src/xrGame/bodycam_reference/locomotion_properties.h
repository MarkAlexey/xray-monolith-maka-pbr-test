#pragma once
#include "grounded_movement.h"
#include "animation_blend.h"
#include <vector>

namespace BodycamReference
{
struct LocomotionCurveKey
{
	float time, value, arrive, leave;
	bool linear = false;
};
class LocomotionPropertyCurve
{
public:
	LocomotionPropertyCurve(std::vector<LocomotionCurveKey> keys, bool oscillate = false);
	double Sample(double time) const;

private:
	std::vector<LocomotionCurveKey> keys_;
	bool oscillate_;
};
struct LocomotionEssential
{
	Vector velocity{}, movementInput{}, actorAcceleration{};
	Rotation aimingRotation, currentControlRotation, actorRotation;
	double speed = 0, movementInputAmount = 0, maxAcceleration = 0, maxBrakingDeceleration = 0, componentScaleZ = 1;
	int gait = 0, rotationMode = 1;
	std::optional<int> engineRifleGait;
	bool isMoving = false, hasMovementInput = false;
};
LocomotionEssential MakeLocomotionEssential(const GroundedState& movement, Rotation control, double componentScaleZ = 1);
using VelocityBlend = std::array<double, 4>; // F, B, L, R.
Vector UnrotateLocomotionVector(const Vector& value, Rotation rotation);
VelocityBlend CalculateVelocityBlend(const Vector& velocity, Rotation actorRotation);
Vector CalculateRelativeAcceleration(const LocomotionEssential& essential);
struct LocomotionPropertyGate
{
	bool animationUpdateSkipped, movementValuesUpdate, shouldMove;
	double walkRunBlend;
};
struct LocomotionPropertyState
{
	bool shouldMove = false;
	VelocityBlend velocityBlend{};
	double diagonalScaleAmount = 0;
	Vector relativeAcceleration{};
	std::array<double, 2> leanAmount{}; // LR, FB.
	double walkRunBlend = 0, strideBlend = 0, standingPlayRate = 1, crouchingPlayRate = 1;
};
class GroundedLocomotionProperties
{
public:
	GroundedLocomotionProperties(LocomotionPropertyCurve walk, LocomotionPropertyCurve run, LocomotionPropertyCurve diagonal);
	LocomotionPropertyGate Update(
		double dt, bool characterValid, const LocomotionEssential& essential, const AnimationCurves& previousPrimaryCurves);
	const LocomotionPropertyState& State() const { return state_; }

private:
	LocomotionPropertyCurve walk_, run_, diagonal_;
	LocomotionPropertyState state_;
};
}
