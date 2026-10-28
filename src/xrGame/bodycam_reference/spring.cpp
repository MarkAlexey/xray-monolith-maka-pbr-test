// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "spring.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace BodycamReference
{
namespace
{
float bits(std::uint32_t value)
{
	float result;
	std::memcpy(&result, &value, 4);
	return result;
}
const float kSmallNumber = bits(0x322bcc77), kInvTwoPi = bits(0x3e22f983), kTwoPi = bits(0x40c90fdb), kHalfPi = bits(0x3fc90fdb);
// 1 + kExpC1 u + kExpC2 u^2 + kExpC3 u^3 approximates e^u; the dampers decay by its reciprocal.
const float kExpC1 = bits(0x3f80f478), kExpC2 = bits(0x3ee6ad0d), kExpC3 = bits(0x3e83b5ca);
struct Pair
{
	float current, velocity;
};
struct SineCosine
{
	float sine, cosine;
};

// Reduces the angle to [-pi/2, pi/2] and evaluates odd and even polynomials for sine and cosine.
SineCosine SinCos(float angle)
{
	const double quotient = std::trunc(float(float(double(angle) * kInvTwoPi) + (angle >= 0 ? .5 : -.5)));
	float y = float(double(angle) - float(double(float(quotient)) * kTwoPi)), sign = 1;
	if (y > kHalfPi)
	{
		y = float(double(kPiF) - y);
		sign = -1;
	}
	else if (y < -kHalfPi)
	{
		y = float(-double(kPiF) - y);
		sign = -1;
	}
	const float y2 = float(double(y) * y);
	float s = float(double(bits(0x3638b88e)) - float(double(y2) * bits(0x32cd365b)));
	s = float(double(float(double(s) * y2)) - bits(0x39500bf1));
	s = float(double(float(double(s) * y2)) + bits(0x3c088886));
	s = float(double(float(double(s) * y2)) - bits(0x3e2aaaab));
	s = float(double(float(double(s) * y2)) + 1);
	s = float(double(s) * y);
	float c = float(double(bits(0x37cfb4c2)) - float(double(y2) * bits(0x348bdd11)));
	c = float(double(float(double(c) * y2)) - bits(0x3ab609aa));
	c = float(double(float(double(c) * y2)) + bits(0x3d2aaaa3));
	c = float(double(float(double(c) * y2)) - .5);
	c = float(double(float(double(c) * y2)) + 1);
	return { s, float(double(c) * sign) };
}

float ApproxExp(float u)
{
	float v = float(double(u) * kExpC3);
	v = float(double(v) + kExpC2);
	v = float(double(v) * u);
	v = float(double(v) + kExpC1);
	v = float(double(v) * u);
	return float(double(v) + 1);
}

Pair Damper(float current, float velocity, float target, float targetVelocity, float dt, float frequency, float ratio)
{
	if (dt <= 0)
		return { current, velocity };
	const float omega = float(double(frequency) * kTwoPi);
	if (omega < kSmallNumber)
		return { float(double(current) + float(double(velocity) * dt)), velocity };
	const float inverseOmega = float(1.0 / omega);
	if (ratio < kSmallNumber)
	{
		const float x = float(double(current) - target), v = float(double(inverseOmega) * velocity);
		const auto sc = SinCos(float(double(omega) * dt));
		return { float(double(float(double(float(double(sc.cosine) * x)) + target)) + float(double(sc.sine) * v)),
			float(double(float(double(sc.cosine) * velocity)) - float(double(float(double(sc.sine) * omega)) * x)) };
	}
	const float equilibrium = float(double(float(double(float(double(inverseOmega) + inverseOmega)) * ratio) * targetVelocity) + target);
	float x = float(double(current) - equilibrium);
	if (ratio == 1)
	{
		const float b = float(double(float(double(x) * omega)) + velocity), u = float(double(omega) * dt);
		float den = float(double(u) * kExpC3);
		den = float(double(den) * u);
		den = float(double(den) + kExpC1);
		den = float(double(den) + float(double(u) * kExpC2));
		den = float(double(den) * u);
		den = float(double(den) + 1);
		const float decay = float(1.0 / den);
		return { float(double(float(double(float(double(float(double(b) * dt)) + x)) * decay)) + equilibrium),
			float(double(float(double(velocity) - float(double(u) * b))) * decay) };
	}
	if (ratio < 1)
	{
		const float damp = float(double(omega) * ratio),
					alpha = float(double(float(std::sqrt(double(float(1.0 - float(double(ratio) * ratio)))))) * omega);
		const float c = float(double(float(double(float(double(damp) * x)) + velocity)) / alpha);
		const auto sc = SinCos(float(double(alpha) * dt));
		const float decay = float(1.0 / ApproxExp(float(double(damp) * dt)));
		const float out = float(double(float(double(float(double(x) * sc.cosine)) + float(double(sc.sine) * c))) * decay);
		const float oscillatory = float(
			double(float(double(float(double(float(double(sc.cosine) * alpha)) * c)) - float(double(float(double(sc.sine) * alpha)) * x))) *
			decay);
		return { float(double(equilibrium) + out), float(double(oscillatory) - float(double(float(double(out) * ratio)) * omega)) };
	}
	const float alpha = float(double(float(std::sqrt(double(float(double(float(double(ratio) * ratio)) - 1))))) * omega),
				damp = float(double(omega) * ratio);
	float b = float(double(float(double(float(double(float(double(damp) - alpha)) * x)) + velocity)) * float(-.5 / alpha));
	const float slow = float(double(alpha) - damp), fast = float(-double(float(double(damp) + alpha))), signedU = float(double(slow) * dt),
				positiveU = float(-signedU);
	float d = float(double(float(double(signedU) * kExpC3)) * positiveU);
	d = float(double(d) + float(double(signedU) * kExpC2));
	d = float(double(d) * positiveU);
	d = float(double(d) + float(double(signedU) * kExpC1));
	x = float(double(float(double(x) - b)) / float(1.0 - d));
	b = float(double(b) / ApproxExp(float(-double(float(double(fast) * dt)))));
	return { float(double(float(double(x) + equilibrium)) + b), float(double(float(double(b) * fast)) + float(double(x) * slow)) };
}

// Limit of the smoothed input before it drives the spring.
constexpr float kDampedLimit = 10.f;
struct Profile
{
	std::array<double, 3> stiffness, damping, speed, smooth, extra, scale, ads;
	int space;
};
const Profile kLookPosition{ { .6, .6, 1 }, { .6, .6, 1 }, { 10, 25, 10 }, { 7, 6, 6 }, { 35, 15, 0 }, { 5, 5, 2.2 }, { 0, 3, 2 },
	kComponentSpace };
const Profile kHipPosition{ { 0, .8, .7 }, { 0, .6, .4 }, { 0, 10, 10 }, { 0, 5, 50 }, { 0, 0, 0 }, { 0, 5, 5 }, { 1, 1, 1 }, kBoneSpace };
const Profile kLookRotation{ { .6, .4, .1 }, { .4, .6, 1 }, { 15, 15, 15 }, { 12, 12, 12 }, { 0, 0, 15 }, { 5, 5, 8 }, { -2, 3, 3 },
	kComponentSpace };
const Profile kHipRotation{ { 0, 1, 1 }, { 0, .4, .4 }, { 0, 7, 7 }, { 0, 50, 50 }, { 0, 0, 0 }, { 0, 5, 5 }, { 1, 1, 1 }, kBoneSpace };
const Profile kMovePosition{ { 0, 1, 0 }, { 1, 1, 0 }, { 0, 25, 0 }, { 0, 2, 0 }, { 0, 0, 0 }, { 0, 10, 0 }, { 1, 0, 0 }, kBoneSpace };
const Profile kMoveRotation{ { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, { 1, 1, 1 }, kBoneSpace };
struct Definition
{
	SwayAxis axis;
	int component;
	const Profile* primary;
	const Profile* hip;
};
const std::array<Definition, 10> kDefinitions{ { { SwayAxis::X, 1, &kLookPosition, &kHipPosition },
	{ SwayAxis::Yaw, 2, &kLookRotation, &kHipRotation }, { SwayAxis::Z, 2, &kLookPosition, &kHipPosition },
	{ SwayAxis::Roll, 1, &kLookRotation, &kHipRotation }, { SwayAxis::Pitch, 0, &kLookRotation, &kHipRotation },
	{ SwayAxis::X, 1, &kMovePosition, &kMovePosition }, { SwayAxis::Yaw, 2, &kMoveRotation, &kMoveRotation },
	{ SwayAxis::Pitch, 0, &kMoveRotation, &kMoveRotation }, { SwayAxis::Z, 2, &kMovePosition, &kMovePosition },
	{ SwayAxis::Roll, 1, &kMoveRotation, &kMoveRotation } } };
}

float ExponentialDamp(float current, float target, double dt, double speed)
{
	const float t = float(dt), s = std::max(float(speed), 0.f);
	const float alpha = s > float(1e-8) ? float(1.0 - float(std::exp(-double(float(double(s) * std::max(t, 0.f)))))) : 1;
	return float(double(float(double(float(double(target) - current)) * alpha)) + current);
}
float FloatSpring(float current, float target, SpringState& state, double stiffness, double damping, double delta)
{
	const float dt = float(delta);
	if (dt <= kSmallNumber)
		return current;
	const float targetVelocity = state.initialized ? float(double(float(double(target) - state.previousTarget)) / dt) : 0;
	const float frequency = float(double(float(std::sqrt(double(float(stiffness))))) * kInvTwoPi);
	const auto result = Damper(current, state.velocity, target, targetVelocity, dt, frequency, float(damping));
	state.previousTarget = target;
	state.initialized = true;
	state.velocity = result.velocity;
	return result.current;
}
std::array<SwayNode, 10> SwayRig::Update(const SpringInput& input)
{
	std::array<SwayNode, 10> result{};
	for (int index : kGraphOrder)
	{
		auto& state = states_[index];
		const auto& d = kDefinitions[index];
		const bool moveNode = index >= 5;
		const double weight =
			input.hasPublishedAlpha ? (moveNode ? input.actualMove : input.actualLook) : (moveNode ? 1 - input.adsAlpha : 1);
		const float alpha = float(std::clamp(weight, 0.0, 1.0));
		auto& out = result[index];
		out = { d.axis, 0, alpha, d.primary->space, alpha > kBlendEpsilon };
		if (!out.relevant)
			continue;
		const auto& p = *(input.aiming ? d.primary : d.hip);
		const int c = d.component;
		const bool magnifiedNode = index == 0 || index == 2;
		const bool useAds = magnifiedNode ? (input.aiming && input.magnified) : (moveNode && input.aiming);
		const double adsScale = magnifiedNode ? input.magnifiedScale[c] * p.ads[c] : 1;
		const float target = float(double(float(input.values[index])) * float(useAds ? adsScale : p.scale[c]));
		state.damped = std::clamp(ExponentialDamp(state.damped, target, input.dt, p.smooth[c]), -kDampedLimit, kDampedLimit);
		const float springDt = std::min(float(double(float(input.dt)) * float(p.speed[c])), 1.f);
		state.springValue = FloatSpring(state.springValue, state.damped, state, p.stiffness[c], p.damping[c], springDt);
		state.output = ExponentialDamp(state.output, state.springValue, input.dt, moveNode ? 0 : p.extra[c]);
		out.value = state.output;
	}
	return result;
}
}
