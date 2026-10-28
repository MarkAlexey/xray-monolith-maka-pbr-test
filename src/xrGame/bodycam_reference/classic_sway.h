#pragma once
#include "ads_sway.h"
#include "classic_follow.h"
#include "spring.h"
#include "transform.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace BodycamReference
{
struct ClassicSwayInput
{
	// The view as a control rotation in degrees, before camera lag, bob and shot effects.
	Rotation control;
	double dt = 0;
	bool enabled = false, aiming = false, magnified = false;
	// Scales the look input of the springs. At 0 they are reset.
	double lookWeight = 1;
};
struct ClassicSwayOutput
{
	std::array<SwayNode, 10> nodes{};
	double lookX = 0, lookY = 0, yawRate = 0, pitchRate = 0;
	bool ownsLook = false;
};
class ClassicLookSway
{
public:
	void Reset()
	{
		springs_.Reset();
		ads_ = AdsSwayProducer{};
		seeded_ = false;
		previous_ = {};
	}
	ClassicSwayOutput Update(const ClassicSwayInput& in)
	{
		ClassicSwayOutput out;
		if (!in.enabled || !std::isfinite(in.dt) || in.dt <= 0 || in.dt > kClassicMaxFrameSeconds || !Finite(in.control) ||
			!std::isfinite(in.lookWeight))
		{
			Reset();
			return out;
		}
		out.ownsLook = true;
		const double weight = std::clamp(in.lookWeight, 0., 1.);
		if (!seeded_ || weight == 0)
		{
			springs_.Reset();
			ads_.SeedControl(in.control);
			previous_ = in.control;
			seeded_ = true;
			return out;
		}
		const auto look = ClassicLookInput(in.control, previous_);
		out.lookX = look[0];
		out.lookY = look[1];
		const double dt = ClampFrameDelta(in.dt);
		out.yawRate = out.lookX / dt / kLookRateScale;
		out.pitchRate = out.lookY / dt / kLookRateScale;
		previous_ = in.control;
		SpringInput spring;
		spring.values = { out.yawRate, out.yawRate, out.pitchRate, out.pitchRate, out.yawRate, 0, 0, 0, 0, 0 };
		spring.dt = dt;
		spring.aiming = in.aiming;
		spring.magnified = in.magnified;
		spring.magnifiedScale = ads_.Update(in.control, dt);
		spring.hasPublishedAlpha = true;
		spring.actualLook = weight;
		spring.actualMove = 0;
		out.nodes = springs_.Update(spring);
		return out;
	}

private:
	static bool Finite(Rotation r) { return std::isfinite(r.pitch) && std::isfinite(r.yaw) && std::isfinite(r.roll); }
	SwayRig springs_;
	AdsSwayProducer ads_;
	Rotation previous_{};
	bool seeded_ = false;
};
// gun and head share one space, in the library's axes and metres. The springs act in the head's local
// space, so their lateral and vertical directions follow the view. The spring pivot is zero.
inline Matrix ApplyClassicLookSway(const Matrix& gun, const Matrix& head, const ClassicSwayOutput& sway)
{
	if (!sway.ownsLook)
		return gun;
	const Matrix local = Multiply(InverseAffine(head), gun);
	return Multiply(head, ApplySpringStack(local, kIdentityMatrix, sway.nodes, Vector{}));
}
}