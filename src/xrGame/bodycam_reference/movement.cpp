// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "movement.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr double kMinAnalogWalkSpeed = 25;
constexpr unsigned kMaxWalkingSteps = 8;
double F(double v)
{
	return static_cast<float>(v);
}
double Clamp(double v, double a, double b)
{
	return std::max(a, std::min(b, v));
}
double Square(PlanarVector v)
{
	return v.x * v.x + v.y * v.y;
}
PlanarVector Normalized(PlanarVector v)
{
	const double n = Square(v);
	return n < F(1e-8) ? PlanarVector{} : PlanarVector{ v.x / std::sqrt(n), v.y / std::sqrt(n) };
}
PlanarVector ClampSize(PlanarVector v, double maximum)
{
	if (maximum < F(1e-4))
		return {};
	const double n = Square(v);
	if (n <= maximum * maximum)
		return v;
	const double scale = maximum / std::sqrt(n);
	return { v.x * scale, v.y * scale };
}
double Map(double v, double a, double b, double c, double d)
{
	return c + (d - c) * Clamp((v - a) / (b - a), 0, 1);
}
bool Finite(PlanarVector v)
{
	return std::isfinite(v.x) && std::isfinite(v.y);
}
}
PlanarVector FixDiagonal(double x, double y)
{
	const auto boost = [](double v) { return 1 + (F(1.2) - 1) * Clamp(std::abs(v) / F(.6), 0, 1); };
	return { Clamp(x * boost(y), -1, 1), Clamp(y * boost(x), -1, 1) };
}
PlanarVector PlayerMovementInput(double x, double y, double cachedYaw, int movementState, const MovementBasisProvider& provider)
{
	if (movementState != 1 && movementState != 2)
		return {};
	const auto fixed = FixDiagonal(x, y);
	const double right = F(fixed.x), forward = F(fixed.y);
	if (provider)
	{
		const auto basis = provider(cachedYaw);
		return { basis.forward.x * forward + basis.right.x * right, basis.forward.y * forward + basis.right.y * right };
	}
	const double yaw = F(cachedYaw) * kPi / 180, c = std::cos(yaw), s = std::sin(yaw);
	return { c * forward - s * right, s * forward + c * right };
}
PlanarInput PlanarAcceleration(PlanarVector input, double maximum)
{
	maximum = F(maximum);
	const double squared = Square(input), factor = squared > 1 ? 1 / std::sqrt(squared) : 1;
	const PlanarVector a{ input.x * factor * maximum, input.y * factor * maximum };
	const double n = Square(a);
	return { a, n > 0 && maximum > F(1e-8) ? F(Clamp(std::sqrt(n) * (1 / maximum), 0, 1)) : 0 };
}
double VectorLength(const Vector& v)
{
	return std::sqrt((v[1] * v[1] + v[0] * v[0]) + v[2] * v[2]);
}
double MovementInputAmount(const Vector& acceleration, double maximum)
{
	const double divisor = F(maximum);
	return divisor == 0 || std::isnan(divisor) ? 0 : VectorLength(acceleration) / divisor;
}
Vector ObservedAcceleration(const Vector& velocity, const Vector& previous, double worldDelta)
{
	const double d = F(worldDelta);
	if (d == 0 || std::isnan(d))
		return {};
	const double reciprocal = 1 / d;
	return { (velocity[0] - previous[0]) * reciprocal, (velocity[1] - previous[1]) * reciprocal, (velocity[2] - previous[2]) * reciprocal };
}
const MovementRow& TargetMovementSettings(unsigned rotation, unsigned stance)
{
	static const MovementRow rows[3][2] = { { { 150, 325, 500 }, { 150, 200, 300 } }, { { 125, 325, 450 }, { 100, 200, 300 } },
		{ { 100, 325, 600 }, { 50, 200, 300 } } };
	if (rotation > 2 || stance > 1)
		throw std::invalid_argument("Movement rotation mode or stance out of range");
	return rows[rotation][stance];
}
double MappedMovementSpeed(double speed, const MovementRow& s)
{
	if (speed > s.run)
		return Map(speed, s.run, s.sprint, 2, 3);
	if (speed > s.walk)
		return Map(speed, s.walk, s.run, 1, 2);
	return Map(speed, 0, s.walk, 0, 1);
}
int ActualGait(double speed, int allowed, const MovementRow& s)
{
	if (speed >= s.run + 10)
		return allowed == 2 ? 2 : 1;
	return speed >= s.walk + 10 ? 1 : 0;
}
bool CanSprint(bool hasInput, double amount, int rotation, double yawDelta, bool canAccelerate, double bombDefuse)
{
	if (!hasInput)
		return false;
	if (rotation == 0)
		return amount > F(.9);
	if (rotation == 1)
		return amount > .5 && std::abs(yawDelta) < 100 && canAccelerate && bombDefuse == 0;
	return false;
}
int AllowedGait(int stance, int rotation, int desired, bool sprintAllowed)
{
	if (stance == 1 || rotation == 2 || desired == 0)
		return 0;
	return sprintAllowed ? desired : 0;
}
MovementSettings DynamicMovementSettings(
	double speed, int allowed, const MovementRow& row, double weaponSpeed, const MovementCurveSampler& sample)
{
	const double mapped = MappedMovementSpeed(speed, row), multiplier = weaponSpeed != 0 ? weaponSpeed : 1;
	const double speeds[]{ row.walk, row.run, row.sprint };
	return { mapped, F(speeds[allowed] * multiplier), F(sample(0, F(mapped))), F(sample(1, F(mapped))), F(sample(2, F(mapped))) };
}
bool ExceedsPlanarSpeed(PlanarVector v, double maximum)
{
	const double m = F(std::max(0.0, F(maximum)));
	return Square(v) > F(F(m * m) * F(1.01));
}
PlanarVector CalculatePlanarVelocity(PlanarVector v, PlanarVector acceleration, double dt, const MovementSettings& settings)
{
	dt = F(dt);
	if (dt < F(1e-6))
		return v;
	const double friction = F(std::max(0.0, settings.groundFriction)), braking = F(std::max(0.0, settings.brakingDeceleration));
	const double maxInput = F(std::max(F(F(settings.maxWalkSpeed) * F(settings.analogInputModifier)), kMinAnalogWalkSpeed));
	const bool zero = acceleration.x == 0 && acceleration.y == 0, over = ExceedsPlanarSpeed(v, maxInput);
	if (zero || over)
	{
		const auto old = v;
		if (braking != 0 && Square(v) != 0)
		{
			const auto direction = Normalized(v);
			v.x += -braking * direction.x * dt;
			v.y += -braking * direction.y * dt;
			const double speedSquared = F(Square(v));
			if (v.x * old.x + v.y * old.y <= 0 || speedSquared <= 100)
				v = {};
		}
		if (over && Square(v) < F(maxInput * maxInput) && old.x * acceleration.x + old.y * acceleration.y > 0)
		{
			const auto direction = Normalized(old);
			v = { direction.x * maxInput, direction.y * maxInput };
		}
	}
	else
	{
		const auto direction = Normalized(acceleration);
		const double speed = F(std::sqrt(Square(v))), alpha = std::min(F(dt * friction), 1.0);
		v.x -= (v.x - direction.x * speed) * alpha;
		v.y -= (v.y - direction.y * speed) * alpha;
	}
	if (!zero)
	{
		const double limit = ExceedsPlanarSpeed(v, maxInput) ? F(std::sqrt(Square(v))) : maxInput;
		v.x += acceleration.x * dt;
		v.y += acceleration.y * dt;
		v = ClampSize(v, limit);
	}
	return v;
}
double WalkingTimeSlice(double remaining, unsigned iteration)
{
	remaining = F(remaining);
	double slice = remaining;
	if (remaining > F(.05) && iteration < kMaxWalkingSteps)
		slice = std::min(F(remaining * F(.5)), F(.05));
	return std::max(slice, F(1e-6));
}
WalkingResult IntegrateFlatWalking(PlanarVector position, PlanarVector velocity, PlanarVector acceleration, double dt,
	const MovementSettings& settings, const WalkingResolver& resolve)
{
	if (!Finite(position) || !Finite(velocity) || !Finite(acceleration))
		throw std::invalid_argument("Walking position, velocity or acceleration is not finite");
	WalkingResult result{ position, velocity, {}, F(dt) };
	result.steps.reserve(kMaxWalkingSteps);
	unsigned iteration = 0;
	while (result.remaining >= F(1e-6) && iteration < kMaxWalkingSteps)
	{
		const double slice = WalkingTimeSlice(result.remaining, ++iteration);
		result.remaining = F(result.remaining - slice);
		const auto before = result.position;
		result.velocity = CalculatePlanarVelocity(result.velocity, acceleration, slice, settings);
		const PlanarVector delta{ result.velocity.x * slice, result.velocity.y * slice };
		const bool tiny = std::abs(delta.x) <= F(1e-4) && std::abs(delta.y) <= F(1e-4);
		if (tiny)
		{
			result.remaining = 0;
			result.exit = WalkingExit::TinyDisplacement;
		}
		else
		{
			const PlanarVector proposed{ before.x + delta.x, before.y + delta.y };
			result.position = resolve ? resolve({ before, delta, proposed, slice, iteration }) : proposed;
			if (!Finite(result.position))
				throw std::invalid_argument("Walking resolver returned a nonfinite position");
		}
		const double reciprocal = 1 / slice;
		result.velocity = { (result.position.x - before.x) * reciprocal, (result.position.y - before.y) * reciprocal };
		result.steps.push_back({ iteration, slice, result.remaining, before, delta, result.position, result.velocity, tiny });
		if (result.position.x == before.x && result.position.y == before.y)
		{
			if (!tiny)
				result.exit = WalkingExit::NoDisplacement;
			break;
		}
	}
	if (iteration == kMaxWalkingSteps && result.remaining >= F(1e-6))
		result.exit = WalkingExit::IterationLimit;
	return result;
}
}
