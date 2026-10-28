// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "grounded_movement.h"
#include "camera.h"
#include "constants.h"
#include <cmath>
#include <utility>

namespace BodycamReference
{
namespace
{
double F(double v)
{
	return static_cast<float>(v);
}
double Normalize(double v)
{
	v = std::fmod(std::fmod(v, 360) + 360, 360);
	return v > 180 ? v - 360 : v;
}
}
MovementBasis YawMovementBasis(double cachedYaw)
{
	const double yaw = F(cachedYaw), forward = std::fmod(yaw, 360) * kRadiansPerDegree, right = yaw * double(kRadiansPerDegreeF);
	return { { std::cos(forward), std::sin(forward) }, { -std::sin(right), std::cos(right) } };
}
GroundedMovement::GroundedMovement(MovementCurveSampler sample, MovementBasisProvider basis)
	: sample_(std::move(sample)), basis_(std::move(basis))
{
	state_.settings = DynamicMovementSettings(0, 0, state_.currentRow, 1, sample_);
}
const GroundedState& GroundedMovement::Step(const GroundedInput& input)
{
	auto& s = state_;
	s.rotationMode = input.aiming ? 2 : 1;
	// The component ticks before its owning actor: cached yaw and settings are prior-frame values.
	const auto raw = PlayerMovementInput(input.right, input.forward, s.cachedControl.yaw, 1, basis_);
	const auto processed = PlanarAcceleration(raw, s.settings.maxAcceleration);
	s.acceleration = processed.acceleration;
	s.analogInputModifier = processed.analogInputModifier;
	auto walkingSettings = s.settings;
	walkingSettings.analogInputModifier = s.analogInputModifier;
	if (input.engineVelocity)
	{
		s.walking.reset();
		s.velocity = *input.engineVelocity;
		s.position = input.position.value_or(s.position);
	}
	else
	{
		s.walking = IntegrateFlatWalking(
			input.position.value_or(s.position), s.velocity, s.acceleration, F(input.dt), walkingSettings, input.resolveMove);
		s.velocity = s.walking->velocity;
		s.position = s.walking->position;
	}
	s.cachedControl = InterpolateControlRotation(s.cachedControl, input.engineControl, input.worldDelta, 30);
	s.actorAcceleration = ObservedAcceleration({ s.velocity.x, s.velocity.y, 0 }, s.previousVelocity, input.worldDelta);
	s.inputAmount = MovementInputAmount({ s.acceleration.x, s.acceleration.y, 0 }, s.settings.maxAcceleration);
	const bool hasInput = s.inputAmount > 0, sprintPresentation = input.sprintRequested && !input.aiming;
	s.desiredGait = input.desiredGait.value_or(sprintPresentation && hasInput ? 1 : 0);
	const double direction = std::atan2(s.acceleration.y, s.acceleration.x) * 180 / kPi;
	const bool sprint = CanSprint(
		hasInput, s.inputAmount, s.rotationMode, F(Normalize(direction - s.cachedControl.yaw)), input.canAccelerate, input.bombDefuse);
	s.allowedGait = AllowedGait(input.stance, s.rotationMode, s.desiredGait, sprint);
	const double speed = VectorLength({ s.velocity.x, s.velocity.y, 0 });
	// actualGait follows the measured speed; engineRifleGait picks the walk/jog or sprint clips when the host supplies the velocity.
	s.actualGait = ActualGait(speed, s.allowedGait, s.currentRow);
	s.engineRifleGait =
		input.engineVelocity && input.engineGaitPresentation ? std::optional<int>(sprintPresentation ? 1 : 0) : std::nullopt;
	s.currentRow = TargetMovementSettings(s.rotationMode, input.stance);
	s.settings = DynamicMovementSettings(speed, s.allowedGait, s.currentRow, input.weaponSpeed, sample_);
	s.actorYaw = F(s.cachedControl.yaw) * kPi / 180;
	s.previousVelocity = { s.velocity.x, s.velocity.y, 0 };
	return s;
}
void GroundedMovement::SyncWorldVelocity(double vx, double vz)
{
	state_.velocity = { vz * kCentimetresPerMetre, -vx * kCentimetresPerMetre };
}
void GroundedMovement::SeedControl(Rotation control, double vx, double vz)
{
	state_.cachedControl = control;
	state_.actorYaw = F(control.yaw) * kPi / 180;
	SyncWorldVelocity(vx, vz);
	state_.previousVelocity = { state_.velocity.x, state_.velocity.y, 0 };
}
Rotation GroundedMovement::AimingAngle() const
{
	// Aiming yaw is control yaw minus actor yaw, and the actor yaw is the float-rounded control yaw, so only the rounding residue remains.
	return { F(Normalize(state_.cachedControl.pitch)), F(Normalize(state_.cachedControl.yaw - F(state_.cachedControl.yaw))), 0 };
}
}
