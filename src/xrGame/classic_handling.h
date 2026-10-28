#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Movement-linked handling for the Classic rig, free of engine types: the
// weapon shifts against the actor's acceleration and dips at each foot contact.
// The caller runs one update per frame and publishes the translation.
namespace Bodycam
{
namespace Classic
{

struct Vec3
{
	double x = 0, y = 0, z = 0;
};
inline Vec3 Add(Vec3 a, Vec3 b)
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline Vec3 Sub(Vec3 a, Vec3 b)
{
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline Vec3 Scale(Vec3 a, double s)
{
	return { a.x * s, a.y * s, a.z * s };
}
inline double Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline double Length(Vec3 a)
{
	return std::sqrt(Dot(a, a));
}
inline Vec3 Limit(Vec3 v, double cap)
{
	const double n = Length(v);
	return n > cap ? Scale(v, cap / n) : v;
}

// A longer frame (a pause, a load) restarts the observation.
constexpr double kMaxFrameTime = 0.25; // seconds
// A velocity step beyond ordinary locomotion (a teleport) restarts it too.
constexpr double kMaxVelocityStep = 25.; // m/s in one frame
// Share of the motion removed at full ADS.
constexpr double kAdsReduction = 0.9;

// Acceleration shift.
constexpr double kMaxAcceleration = 18.; // m/s^2
constexpr double kAccelerationSmoothing = 20.; // 1/s
constexpr double kShiftPerAcceleration = 0.0012; // metres per m/s^2, against the acceleration
constexpr double kMaxShift = 0.024; // metres
constexpr double kShiftFollowRate = 16.; // 1/s

// Foot contact dip.
constexpr double kContactMinSpeed = 0.05; // m/s; slower contacts do not dip.
constexpr double kContactFullSpeed = 4.; // m/s at which a contact has full strength.
constexpr double kContactImpulse = 0.035; // m/s downward at full strength
constexpr double kContactFollowRate = 22.; // 1/s
constexpr double kMaxContactDip = 0.006; // metres

struct HandlingInput
{
	double dt = 0;
	bool enabled = false;
	// The measured physics velocity, not the movement keys.
	Vec3 world_velocity;
	// World axes of the space the translation is published in.
	Vec3 right{ 1, 0, 0 }, up{ 0, 1, 0 }, forward{ 0, 0, 1 };
	bool grounded = true;
	double ads_weight = 0; // 0..1
	// An authored action owns the weapon: nothing new is excited, and what is
	// already in motion settles.
	bool suppressed = false;
	std::uint64_t contact_serial = 0; // Changes at each foot contact.
};

struct HandlingSpring
{
	Vec3 value, velocity;
};
struct HandlingState
{
	bool initialized = false;
	Vec3 previous_world_velocity, acceleration;
	std::uint64_t contact_serial = 0;
	HandlingSpring movement, contact;
};

inline Vec3 ToLocal(Vec3 world, const HandlingInput& in)
{
	return { Dot(world, in.right), Dot(world, in.up), Dot(world, in.forward) };
}

// Exact critically damped integration for a constant target over this interval,
// so the result does not depend on the frame rate.
inline void Follow(HandlingSpring& s, Vec3 target, double rate, double dt)
{
	const Vec3 error = Sub(s.value, target);
	const Vec3 coupled = Add(s.velocity, Scale(error, rate));
	const double decay = std::exp(-rate * dt);
	s.value = Add(target, Scale(Add(error, Scale(coupled, dt)), decay));
	s.velocity = Scale(Sub(s.velocity, Scale(coupled, rate * dt)), decay);
}

// Returns the weapon translation in the space of the input axes, metres.
inline Vec3 Update(HandlingState& state, const HandlingInput& in)
{
	if (!in.enabled || !(in.dt > 0 && in.dt <= kMaxFrameTime))
	{
		state = {};
		return {};
	}
	if (state.initialized && Length(Sub(in.world_velocity, state.previous_world_velocity)) > kMaxVelocityStep)
		state = {};
	if (!state.initialized)
	{
		state.initialized = true;
		state.previous_world_velocity = in.world_velocity;
		state.contact_serial = in.contact_serial;
	}
	const Vec3 velocity_change = Sub(in.world_velocity, state.previous_world_velocity);
	state.previous_world_velocity = in.world_velocity;
	// The weight scales what is newly excited; the published offset itself is
	// never scaled, so a change of weight cannot make it jump.
	const double weight = in.suppressed ? 0. : 1. - kAdsReduction * in.ads_weight;
	// The velocity is differentiated in world space: turning the camera at a
	// constant world velocity is not an acceleration.
	Vec3 acceleration = Scale(velocity_change, 1. / in.dt);
	acceleration.y = 0; // The jump and landing animations own vertical motion.
	acceleration = Limit(acceleration, kMaxAcceleration);
	state.acceleration =
		Add(state.acceleration, Scale(Sub(acceleration, state.acceleration), 1. - std::exp(-kAccelerationSmoothing * in.dt)));
	const Vec3 target = Limit(Scale(ToLocal(state.acceleration, in), -kShiftPerAcceleration * weight), kMaxShift);
	Follow(state.movement, target, kShiftFollowRate, in.dt);

	const bool contact = in.contact_serial != state.contact_serial;
	state.contact_serial = in.contact_serial; // A suppressed or airborne contact is consumed too.
	const double planar_speed = std::hypot(in.world_velocity.x, in.world_velocity.z);
	if (contact && in.grounded && planar_speed > kContactMinSpeed)
		state.contact.velocity.y -= kContactImpulse * (std::min(planar_speed / kContactFullSpeed, 1.) * weight);
	Follow(state.contact, {}, kContactFollowRate, in.dt);
	state.contact.value = Limit(state.contact.value, kMaxContactDip);
	return Add(state.movement.value, state.contact.value);
}

}
} // namespace Bodycam::Classic
