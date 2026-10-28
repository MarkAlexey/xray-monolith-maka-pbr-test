// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "locomotion_properties.h"
#include "constants.h"
#include "recoil_curve.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
// Speeds (cm/s) at which the walk, run, sprint and crouch cycles play at rate one.
constexpr double kWalkCycleSpeed = 150, kRunCycleSpeed = 350, kSprintCycleSpeed = 600, kCrouchCycleSpeed = 150;
constexpr double kVelocityBlendSpeed = 12, kLeanSpeed = 4;
double F(double v)
{
	return static_cast<float>(v);
}
double Divide(double a, double b)
{
	return b == 0 || std::isnan(b) ? 0 : a / b;
}
double Lerp(double a, double b, double t)
{
	return (b - a) * t + a;
}
double Clamp(double v, double a, double b)
{
	return std::max(a, std::min(v, b));
}
Vector DivideVector(Vector v, double d)
{
	if (d == 0 || std::isnan(d))
		return {};
	const double reciprocal = 1 / d;
	for (auto& c : v)
		c *= reciprocal;
	return v;
}
double Curve(const AnimationCurves& curves, const char* name)
{
	const auto it = curves.find(name);
	return it == curves.end() ? 0 : it->second;
}
int NearestEven(double v)
{
	const double lo = std::floor(v), part = v - lo;
	return static_cast<int>(part < .5 ? lo : part > .5 ? lo + 1 : std::fmod(lo, 2) == 0 ? lo : lo + 1);
}
}
LocomotionPropertyCurve::LocomotionPropertyCurve(std::vector<LocomotionCurveKey> keys, bool oscillate)
	: keys_(std::move(keys)), oscillate_(oscillate)
{
	if (keys_.size() < 2)
		throw std::invalid_argument("Locomotion curve needs at least two keys");
	for (unsigned i = 0; i < keys_.size(); ++i)
	{
		const auto& k = keys_[i];
		if (!std::isfinite(k.time) || !std::isfinite(k.value) || !std::isfinite(k.arrive) || !std::isfinite(k.leave) ||
			(i && k.time <= keys_[i - 1].time))
			throw std::invalid_argument("Locomotion curve keys must be finite and increase in time");
	}
	if (oscillate_ && (keys_.front().time != 0 || keys_.back().time != .5f))
		throw std::invalid_argument("Oscillating locomotion curve must span 0 to 0.5");
}
double LocomotionPropertyCurve::Sample(double time) const
{
	double t = F(time);
	const double first = keys_.front().time, last = keys_.back().time;
	if (oscillate_)
	{
		if (t > last)
		{
			const double original = t, span = F(last - first), ratio = F(F(last - t) / span);
			// Halving rounds towards negative infinity, odd negative counts included.
			const int cycles = static_cast<int>(std::floor(NearestEven(F(F(ratio + ratio) - .5)) / 2.0));
			t = F(F(F(cycles) * span) + t);
			if (t == first && original > last)
				t = last;
			if (std::abs(cycles) % 2 == 1)
				t = F(F(last - t) + first);
		}
	}
	double value;
	if (t < first)
		value = keys_.front().value;
	else if (t > last)
		value = keys_.back().value;
	else
	{
		unsigned i = 0;
		while (i + 2 < keys_.size() && t >= keys_[i + 1].time)
			++i;
		const auto& a = keys_[i];
		const auto& b = keys_[i + 1];
		const double span = F(double(b.time) - a.time), u = F(F(t - a.time) / span);
		if (a.linear)
			value = F(F(F(double(b.value) - a.value) * u) + a.value);
		else
		{
			const double p1 = F(a.value + F(F(span * a.leave) * F(1.0 / 3))), p2 = F(b.value - F(F(span * b.arrive) * F(1.0 / 3)));
			value = CubicBezier(a.value, static_cast<float>(p1), static_cast<float>(p2), b.value, static_cast<float>(u));
		}
	}
	return F(value + 0);
}
LocomotionEssential MakeLocomotionEssential(const GroundedState& m, Rotation control, double scale)
{
	LocomotionEssential e;
	e.velocity = { m.velocity.x, m.velocity.y, 0 };
	e.movementInput = { m.acceleration.x, m.acceleration.y, 0 };
	e.actorAcceleration = m.actorAcceleration;
	e.aimingRotation = m.cachedControl;
	e.currentControlRotation = control;
	e.actorRotation = { 0, F(m.cachedControl.yaw), 0 };
	e.speed = VectorLength(e.velocity);
	e.movementInputAmount = MovementInputAmount(e.movementInput, m.settings.maxAcceleration);
	e.maxAcceleration = F(m.settings.maxAcceleration);
	e.maxBrakingDeceleration = F(m.settings.brakingDeceleration);
	e.componentScaleZ = scale;
	e.gait = m.actualGait;
	e.rotationMode = m.rotationMode;
	e.engineRifleGait = m.engineRifleGait;
	e.isMoving = e.speed > 1;
	e.hasMovementInput = e.movementInputAmount > 0;
	return e;
}
Vector UnrotateLocomotionVector(const Vector& v, Rotation r)
{
	const double degree = double(kRadiansPerDegreeF), sp = std::sin(r.pitch * degree), cp = std::cos(r.pitch * degree),
				 sy = std::sin(r.yaw * degree), cy = std::cos(r.yaw * degree), sr = std::sin(r.roll * degree),
				 cr = std::cos(r.roll * degree);
	const Vector rows[]{ { cy * cp, cp * sy, sp }, { ((sr * sp) * cy) - (cr * sy), ((sr * sp) * sy) + (cr * cy), -(cp * sr) },
		{ -(((cr * sp) * cy) + (sr * sy)), (cy * sr) - ((cr * sp) * sy), cr * cp } };
	Vector out;
	for (unsigned i = 0; i < 3; ++i)
		out[i] = ((v[1] * rows[i][1] + v[0] * rows[i][0]) + v[2] * rows[i][2]) + 0;
	return out;
}
VelocityBlend CalculateVelocityBlend(const Vector& velocity, Rotation actorRotation)
{
	const double squared = (velocity[0] * velocity[0] + velocity[1] * velocity[1]) + velocity[2] * velocity[2];
	auto normal = velocity;
	if (squared < F(.1))
		normal = {};
	else if (squared != 1)
	{
		const double reciprocal = 1 / std::sqrt(squared);
		for (auto& v : normal)
			v *= reciprocal;
	}
	const auto local = UnrotateLocomotionVector(normal, actorRotation);
	const auto direction = DivideVector(local, (std::abs(local[0]) + std::abs(local[1])) + std::abs(local[2]));
	return { F(Clamp(direction[0], 0, 1)), F(std::abs(Clamp(direction[0], -1, 0))), F(std::abs(Clamp(direction[1], -1, 0))),
		F(Clamp(direction[1], 0, 1)) };
}
Vector CalculateRelativeAcceleration(const LocomotionEssential& e)
{
	auto a = e.actorAcceleration;
	const auto& v = e.velocity;
	const double dot = (a[1] * v[1] + a[0] * v[0]) + a[2] * v[2], limit = F(dot > 0 ? e.maxAcceleration : e.maxBrakingDeceleration);
	if (!(limit >= F(.0001)))
		a = {};
	else
	{
		const double squared = (a[1] * a[1] + a[0] * a[0]) + a[2] * a[2];
		if (squared > limit * limit)
		{
			const double scale = (1 / std::sqrt(squared)) * limit;
			for (auto& c : a)
				c *= scale;
		}
	}
	return UnrotateLocomotionVector(DivideVector(a, limit), e.actorRotation);
}
GroundedLocomotionProperties::GroundedLocomotionProperties(
	LocomotionPropertyCurve walk, LocomotionPropertyCurve run, LocomotionPropertyCurve diagonal)
	: walk_(std::move(walk)), run_(std::move(run)), diagonal_(std::move(diagonal))
{
}
LocomotionPropertyGate GroundedLocomotionProperties::Update(
	double dt, bool valid, const LocomotionEssential& e, const AnimationCurves& prior)
{
	dt = F(dt);
	auto& s = state_;
	if (dt == 0 || !valid)
		return { true, false, s.shouldMove, s.walkRunBlend };
	s.shouldMove = (e.isMoving && e.hasMovementInput) || e.speed > 150;
	if (!s.shouldMove)
		return { false, false, false, s.walkRunBlend };
	const auto target = CalculateVelocityBlend(e.velocity, e.actorRotation);
	for (unsigned i = 0; i < 4; ++i)
		s.velocityBlend[i] = F(FInterpTo(F(s.velocityBlend[i]), F(target[i]), dt, kVelocityBlendSpeed));
	s.diagonalScaleAmount = diagonal_.Sample(F(std::abs(F(s.velocityBlend[0]) + F(s.velocityBlend[1]))));
	s.relativeAcceleration = CalculateRelativeAcceleration(e);
	s.leanAmount[0] = F(FInterpTo(F(s.leanAmount[0]), F(s.relativeAcceleration[1]), dt, kLeanSpeed));
	s.leanAmount[1] = F(FInterpTo(F(s.leanAmount[1]), F(s.relativeAcceleration[0]), dt, kLeanSpeed));
	s.walkRunBlend = e.gait == 0 ? 0 : 1;
	const double gait = Curve(prior, "Weight_Gait"), base = Curve(prior, "BasePose_CLF"), a = Clamp(gait - 1, 0, 1),
				 b = Clamp(gait - 2, 0, 1), walk = walk_.Sample(e.speed), run = run_.Sample(e.speed);
	s.strideBlend = Lerp(Lerp(walk, run, a), walk, base);
	const double stand =
		Lerp(Lerp(Divide(e.speed, kWalkCycleSpeed), Divide(e.speed, kRunCycleSpeed), a), Divide(e.speed, kSprintCycleSpeed), b);
	s.standingPlayRate = Clamp(Divide(Divide(stand, s.strideBlend), e.componentScaleZ), 0, 3);
	s.crouchingPlayRate = Clamp(Divide(Divide(Divide(e.speed, kCrouchCycleSpeed), s.strideBlend), e.componentScaleZ), 0, 2);
	return { false, true, true, s.walkRunBlend };
}
}
