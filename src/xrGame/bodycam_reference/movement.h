#pragma once
#include "pose.h"
#include <array>
#include <functional>
#include <vector>

namespace BodycamReference
{
struct PlanarVector
{
	double x = 0, y = 0;
};
struct MovementBasis
{
	PlanarVector forward, right;
};
using MovementBasisProvider = std::function<MovementBasis(double)>;
struct PlanarInput
{
	PlanarVector acceleration;
	double analogInputModifier;
};
struct MovementRow
{
	double walk, run, sprint;
};
struct MovementSettings
{
	double mappedSpeed = 0, maxWalkSpeed = 0, maxAcceleration = 0;
	double brakingDeceleration = 0, groundFriction = 0;
	double analogInputModifier = 0;
};
using MovementCurveSampler = std::function<double(unsigned, double)>;
PlanarVector FixDiagonal(double right, double forward);
PlanarVector PlayerMovementInput(
	double right, double forward, double cachedYaw, int movementState, const MovementBasisProvider& basis = {});
PlanarInput PlanarAcceleration(PlanarVector input, double maximum);
double VectorLength(const Vector& value);
double MovementInputAmount(const Vector& acceleration, double maximum);
Vector ObservedAcceleration(const Vector& velocity, const Vector& previous, double worldDelta);
const MovementRow& TargetMovementSettings(unsigned rotationMode, unsigned stance);
double MappedMovementSpeed(double speed, const MovementRow& row);
int ActualGait(double speed, int allowed, const MovementRow& row);
bool CanSprint(bool hasInput, double amount, int rotationMode, double yawDelta, bool canAccelerate = true, double bombDefuse = 0);
int AllowedGait(int stance, int rotationMode, int desired, bool sprintAllowed);
MovementSettings DynamicMovementSettings(
	double speed, int allowed, const MovementRow& row, double weaponSpeed, const MovementCurveSampler& sample);
bool ExceedsPlanarSpeed(PlanarVector velocity, double maximum);
PlanarVector CalculatePlanarVelocity(PlanarVector velocity, PlanarVector acceleration, double dt, const MovementSettings& settings);
double WalkingTimeSlice(double remaining, unsigned iteration);
struct WalkingMove
{
	PlanarVector from, delta, proposed;
	double slice;
	unsigned iteration;
};
using WalkingResolver = std::function<PlanarVector(const WalkingMove&)>;
struct WalkingStep
{
	unsigned iteration;
	double slice, remaining;
	PlanarVector before, delta, position, velocity;
	bool tiny;
};
enum class WalkingExit
{
	TimeExhausted,
	TinyDisplacement,
	NoDisplacement,
	IterationLimit
};
struct WalkingResult
{
	PlanarVector position, velocity;
	std::vector<WalkingStep> steps;
	double remaining;
	WalkingExit exit = WalkingExit::TimeExhausted;
};
// Flat walking in up to eight sub-steps; the resolver (host collision) settles each step.
WalkingResult IntegrateFlatWalking(PlanarVector position, PlanarVector velocity, PlanarVector acceleration, double dt,
	const MovementSettings& settings, const WalkingResolver& resolve = {});
}
