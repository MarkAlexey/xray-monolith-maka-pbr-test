#pragma once
#include "camera.h"
#include "constants.h"
#include "scalar_math.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace BodycamReference
{
// Longer frames reset the Classic producers instead of advancing them.
constexpr double kClassicMaxFrameSeconds = .4;
// The engine's heading and pitch in radians as a control rotation in degrees. The engine's view
// direction is (-cos(p) sin(h), sin(p), cos(p) cos(h)) and the library's basis mirrors X, so the
// yaw is the negated heading.
inline Rotation ClassicControlDegrees(double yaw, double pitch)
{
	return { pitch * kDegreesPerRadian, -yaw * kDegreesPerRadian, 0 };
}
// The look input ControllerDeltas turns into this control change. The engine's ADS sensitivity
// is already in the control angles, so ModifyRawLook's aim scale is not undone here.
inline std::array<double, 2> ClassicLookInput(const Rotation& control, const Rotation& previous)
{
	return { WrapDegrees(control.yaw - previous.yaw) / kControlDegreesPerLook,
		-WrapDegrees(control.pitch - previous.pitch) / kControlDegreesPerLook };
}
struct ClassicFollowInput
{
	double yaw = 0, pitch = 0, dt = 0; // The engine's heading and pitch in radians; seconds.
	double strafe = 0, speedMetres = 0;
	bool enabled = false, aiming = false, leaningPressed = false, reset = false;
	CameraMotionTuning tuning;
};
struct ClassicFollowOutput
{
	double cameraYaw = 0, cameraPitch = 0;
	double armsYawOffset = 0, armsPitchOffset = 0; // The arms endpoint of the previous frame minus the control, radians.
	double lookX = 0, lookY = 0, yawRate = 0, pitchRate = 0;
	bool ownsFollow = false;
};
// Limit the requested position before smoothing outward movement. During
// same-side recovery the camera already supplies a smooth angular trajectory:
// transport the displayed offset along it and discard pending outward travel.
// Filtering that recovery again would let the gun continue outward after stop.
class ClassicLookTranslation
{
public:
	void Reset()
	{
		yaw_ = {};
		pitch_ = {};
	}
	std::array<double, 3> Update(double yaw, double pitch, double cameraYaw, double cameraPitch, double maxYawDegrees,
		double maxPitchDegrees, double amount, double dt, double response)
	{
		if (!std::isfinite(dt) || dt <= 0 || dt > kClassicMaxFrameSeconds)
		{
			Reset();
			return {};
		}
		const double alpha = -std::expm1(-std::max(response, kMinResponse) * dt);
		const double x = yaw_.Update(WrapDegrees((yaw - cameraYaw) * kDegreesPerRadian), maxYawDegrees, alpha);
		const double y = pitch_.Update(WrapDegrees((pitch - cameraPitch) * kDegreesPerRadian), maxPitchDegrees, alpha);
		return { -x * amount, y * amount * kVerticalShare, 0 };
	}

private:
	static constexpr double kMinResponse = .01;
	// The vertical translation as a share of the horizontal one.
	static constexpr double kVerticalShare = .55;
	struct Axis
	{
		double error = 0, target = 0, value = 0;
		double Update(double next, double limit, double alpha)
		{
			if (limit <= 1e-6)
			{
				*this = {};
				return 0;
			}
			if (next * error > 0 && std::abs(next) < std::abs(error) && value * next >= 0)
			{
				// A rapid reversal can shrink the error in one sample too.
				// Bound that step by the same response used for outward travel.
				value *= std::max(next / error, 1. - alpha);
				target = value;
			}
			else
			{
				target = next * error <= 0 ? std::clamp(next / limit, -1., 1.) : std::clamp(target + (next - error) / limit, -1., 1.);
				value += (target - value) * alpha;
			}
			error = next;
			return value;
		}
	};
	Axis yaw_, pitch_;
};

class ClassicFollow
{
public:
	void Reset()
	{
		camera_.Reset();
		previous_ = {};
		seeded_ = false;
	}
	// Starts following from the displayed camera angles while control is observed from the target
	// angles. Passing the target angles for both starts without lag.
	void Seed(double targetYaw, double targetPitch, double displayedYaw, double displayedPitch)
	{
		previous_ = ClassicControlDegrees(targetYaw, targetPitch);
		camera_.SeedControl(ClassicControlDegrees(displayedYaw, displayedPitch));
		seeded_ = true;
	}
	ClassicFollowOutput Update(const ClassicFollowInput& in)
	{
		ClassicFollowOutput out;
		out.cameraYaw = in.yaw;
		out.cameraPitch = in.pitch;
		if (!in.enabled || !std::isfinite(in.yaw) || !std::isfinite(in.pitch) || !std::isfinite(in.dt) || in.dt <= 0 ||
			in.dt > kClassicMaxFrameSeconds || !std::isfinite(in.strafe) || !std::isfinite(in.speedMetres))
		{
			Reset();
			return out;
		}
		if (in.reset)
			Reset();
		const auto control = ClassicControlDegrees(in.yaw, in.pitch);
		if (!seeded_)
		{
			Seed(in.yaw, in.pitch, in.yaw, in.pitch);
			out.ownsFollow = true;
			return out;
		}
		const auto look = ClassicLookInput(control, previous_);
		out.lookX = look[0];
		out.lookY = look[1];
		previous_ = control;
		camera_.SetTuning(in.tuning);
		const auto& state = camera_.Update({ control, ClampFrameDelta(in.dt), out.lookX, out.lookY, in.strafe,
			in.speedMetres * kCentimetresPerMetre, in.aiming, in.leaningPressed, in.aiming ? 2 : 1 });
		out.cameraYaw = -state.yaw;
		out.cameraPitch = state.pitch;
		out.armsYawOffset = -WrapDegrees(state.finalYaw * kDegreesPerRadian - control.yaw) / kDegreesPerRadian;
		out.armsPitchOffset = WrapDegrees(state.finalPitch * kDegreesPerRadian - control.pitch) / kDegreesPerRadian;
		out.yawRate = state.yawRate;
		out.pitchRate = state.pitchRate;
		out.ownsFollow = true;
		return out;
	}
	const CameraState& State() const { return camera_.State(); }

private:
	CameraMotion camera_;
	Rotation previous_{};
	bool seeded_ = false;
};
}
