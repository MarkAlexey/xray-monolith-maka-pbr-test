#pragma once
#include "movement.h"
#include "control.h"
#include <optional>

namespace BodycamReference
{
MovementBasis YawMovementBasis(double cachedYaw);
struct GroundedInput
{
	double dt = 0, worldDelta = 0, right = 0, forward = 0, weaponSpeed = 1, bombDefuse = 0;
	Rotation engineControl;
	bool aiming = false, sprintRequested = false, canAccelerate = true;
	std::optional<int> desiredGait;
	unsigned stance = 0; // 0 standing, 1 crouched.
	bool engineGaitPresentation = true;
	std::optional<PlanarVector> position;
	std::optional<PlanarVector> engineVelocity;
	WalkingResolver resolveMove;
};
struct GroundedState
{
	Rotation cachedControl;
	double actorYaw = 0, analogInputModifier = 0, inputAmount = 0;
	PlanarVector velocity, acceleration, position;
	Vector actorAcceleration{}, previousVelocity{};
	int rotationMode = 1, allowedGait = 0, actualGait = 0, desiredGait = 0;
	std::optional<int> engineRifleGait; // With a host velocity: 0 walk/jog clips, 1 sprint clips.
	MovementRow currentRow = TargetMovementSettings(1, 0);
	MovementSettings settings;
	std::optional<WalkingResult> walking;
};
// Character movement: input, acceleration, gait selection and flat walking.
class GroundedMovement
{
public:
	explicit GroundedMovement(MovementCurveSampler sample, MovementBasisProvider basis = {});
	const GroundedState& Step(const GroundedInput& input);
	const GroundedState& State() const { return state_; }
	void SyncWorldVelocity(double vx, double vz);
	void SeedControl(Rotation control, double vx, double vz);
	Rotation AimingAngle() const;

private:
	MovementCurveSampler sample_;
	MovementBasisProvider basis_;
	GroundedState state_;
};
}
