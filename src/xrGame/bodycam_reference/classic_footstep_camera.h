#pragma once
#include "camera_effects.h"
#include "classic_follow.h"
#include "footstep_camera_data.h"
#include "gait_phase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace BodycamReference
{
// Camera locomotion for Classic: recorded Reference head motion replayed on the engine's footstep schedule.
struct FootstepCameraInput
{
	double dt = 0;
	bool paused = false;
	// Phase of the engine's step-sound schedule: left contact 0, right contact .5.
	std::optional<double> footPhase;
	double speed = 0; // Planar metres/second.
	double forward = 0, right = 0; // Planar velocity in the camera yaw frame.
	bool sprinting = false, grounded = true;
	double bobScale = 1, postureScale = 1, lungeScale = 1;
};
struct FootstepCameraOutput
{
	std::array<double, 3> offset{}; // Camera yaw frame metres: right, up, forward.
	double phase = 0;
	bool locked = false;
	// Foot contacts reached while braking after the engine's leg cycle stopped:
	// bit 0 left, bit 1 right. The host plays these as footsteps.
	unsigned brakeContacts = 0;
	double speed = 0;
};

inline double WrapSignedFootPhase(double value)
{
	return value - std::floor(value + .5);
}

class ClassicFootstepCamera
{
public:
	void Reset() { *this = ClassicFootstepCamera{}; }
	const FootstepCameraOutput& Update(const FootstepCameraInput& in)
	{
		if (in.paused || !(in.dt > 0))
			return out_;
		const double dt = std::min(in.dt, kMaxStepSeconds);
		const auto previous = out_.offset;
		reseek_ = false;
		const double speed = std::max(in.speed, 0.);
		accel_ += ((speed - lastSpeed_) / dt - accel_) * std::min(1., dt * kAccelFollowRate);
		lastSpeed_ = speed;
		TrackPhase(in, dt, speed);
		gait_ += ((in.sprinting ? 1. : 0.) - gait_) * (1 - std::exp(-kFootstepGaitRate * dt));
		ground_ += ((in.grounded ? 1. : 0.) - ground_) * (1 - std::exp(-kGroundFollowRate * dt));
		if (in.sprinting && !sprinting_)
			lunge_ = 0;
		sprinting_ = in.sprinting;
		const double planar = std::hypot(in.forward, in.right);
		if (planar > kMinPlanarVelocity && in.speed > kMinDirectionSpeed)
		{
			forward_ = in.forward / planar;
			right_ = in.right / planar;
		}
		const double walkW = std::min(speed / kFootstepWalkSpeed, 1.);
		const double sprintW = std::clamp((speed - kFootstepWalkSpeed) / (kFootstepSprintSpeed - kFootstepWalkSpeed), 0., 1.);
		const auto weights = DirectionWeights();
		const auto& cycles = FootstepCycles();
		std::array<double, 3> walkPosture{}, walkCycle{}, sprintCycle{};
		for (unsigned c = 0; c < 4; ++c)
		{
			if (weights[c] <= 0)
				continue;
			for (unsigned a = 0; a < 3; ++a)
			{
				walkPosture[a] += cycles[c].posture[a] * weights[c];
				walkCycle[a] += Sample(cycles[c].axes[a], phase_) * weights[c];
			}
		}
		const auto& sprint = cycles[unsigned(FootstepCycle::SprintForward)];
		for (unsigned a = 0; a < 3; ++a)
			sprintCycle[a] = Sample(sprint.axes[a], phase_);
		const double postureFollow = 1 - std::exp(-kFootstepPostureRate * dt);
		std::array<double, 3> lunge{};
		if (lunge_ >= 0)
		{
			const double index = lunge_ * kFootstepLungeRate;
			if (index < kFootstepLungeSamples - 1)
			{
				const unsigned i = unsigned(index);
				const double t = index - i;
				const auto& curve = FootstepSprintLunge();
				for (unsigned a = 0; a < 3; ++a)
					lunge[a] = curve[a][i] * (1 - t) + curve[a][i + 1] * t;
				// Progress with the actual acceleration: a slower native ramp
				// stretches the dive, a plateau plays it at recorded speed.
				const double rate =
					in.sprinting && accel_ > kLungeMinAccel ? std::clamp(accel_ / kReferenceSprintAccel, kLungeMinRate, kLungeMaxRate) : 1.;
				lunge_ += dt * rate;
			}
			else
				lunge_ = -1;
		}
		for (unsigned a = 0; a < 3; ++a)
		{
			const double target = (walkPosture[a] * (1 - gait_) + sprint.posture[a] * gait_) * walkW * ground_;
			posture_[a] += (target - posture_[a]) * postureFollow;
			const double cycle = (walkCycle[a] * (1 - gait_) * walkW + sprintCycle[a] * gait_ * sprintW) * ground_;
			out_.offset[a] = posture_[a] * in.postureScale + cycle * in.bobScale + lunge[a] * in.lungeScale;
		}
		// Crossfade a schedule reseek instead of popping the camera.
		const double fade = std::exp(-kReseekFadeRate * dt);
		for (unsigned a = 0; a < 3; ++a)
		{
			if (reseek_)
				crossfade_[a] = previous[a] - out_.offset[a];
			else
				crossfade_[a] *= fade;
			out_.offset[a] += crossfade_[a];
		}
		out_.phase = phase_;
		out_.speed = speed;
		return out_;
	}
	const FootstepCameraOutput& Output() const { return out_; }

private:
	static constexpr double kMaxStepSeconds = .1;
	static constexpr double kAccelFollowRate = 10, kGroundFollowRate = 10;
	// The travel direction is updated only above these speeds.
	static constexpr double kMinPlanarVelocity = 1e-3, kMinDirectionSpeed = .05;
	// Reference sprint-entry acceleration (125->325 cm/s in about .33 s). The lunge plays at the
	// ratio of the actual acceleration to it, within these limits.
	static constexpr double kReferenceSprintAccel = 6., kLungeMinAccel = .5, kLungeMinRate = .4, kLungeMaxRate = 1.25;
	static constexpr double kReseekFadeRate = 6;
	// A schedule step larger than this is a jump, not a rate.
	static constexpr double kMaxRateStep = .25;
	static constexpr double kRateFollowRate = 8;
	// The schedule is followed exactly for this long after locking on.
	static constexpr double kLockSettleSeconds = .5;
	// A larger phase error snaps to the schedule; a smaller one is corrected by steps at most
	// kDriftCorrection quicker or slower.
	static constexpr double kReseekError = .1, kDriftCorrection = .6, kMinCorrectionRate = .25, kErrorFollowRate = 15;
	// While the body still moves faster than this after the leg cycle ended, the stride continues
	// for at most kMaxBrakeSteps contacts.
	static constexpr double kBrakeMinSpeed = .3;
	static constexpr unsigned kMaxBrakeSteps = 2;
	static constexpr double kCoastDecayRate = 4;
	static double Wrap(double phase) { return phase - std::floor(phase); }
	// Follows the engine's schedule closely without popping when the clip changes.
	void TrackPhase(const FootstepCameraInput& in, double dt, double speed)
	{
		out_.brakeContacts = 0;
		if (in.footPhase && std::isfinite(*in.footPhase))
		{
			const double enginePhase = Wrap(*in.footPhase);
			if (!locked_)
			{
				phase_ = enginePhase;
				rate_ = 0;
				locked_ = true;
				lockAge_ = 0;
			}
			else
			{
				lockAge_ += dt;
				const double step = WrapSignedFootPhase(enginePhase - lastEnginePhase_);
				if (std::abs(step) < kMaxRateStep)
					rate_ += (step / dt - rate_) * std::min(1., dt * kRateFollowRate);
				const double predicted = phase_ + rate_ * dt;
				// A new stride, or a clip change that restarts the step schedule, snaps to the
				// schedule and Update crossfades the resulting offset.
				const double error = WrapSignedFootPhase(enginePhase - predicted);
				const double bound = kDriftCorrection * std::max(rate_, kMinCorrectionRate) * dt;
				if (lockAge_ < kLockSettleSeconds)
					phase_ = enginePhase;
				else if (std::abs(error) > kReseekError)
				{
					phase_ = enginePhase;
					reseek_ = true;
				}
				else
					phase_ = predicted + std::clamp(error * std::min(1., dt * kErrorFollowRate), -bound, bound);
			}
			lastEnginePhase_ = enginePhase;
			strideRate_ = rate_;
			brakeSteps_ = 0;
		}
		else
		{
			locked_ = false;
			const double previous = Wrap(phase_);
			if (in.grounded && speed > kBrakeMinSpeed && strideRate_ > 0 && brakeSteps_ < kMaxBrakeSteps)
			{
				// The body is still braking: keep the last cadence (braking shortens steps, not
				// their rhythm) and report each contact reached.
				rate_ = strideRate_;
				phase_ += rate_ * dt;
				const float from = float(previous), to = float(Wrap(phase_));
				if (FootContactCrossed(from, to, 0.f))
				{
					out_.brakeContacts |= 1;
					++brakeSteps_;
				}
				if (brakeSteps_ < kMaxBrakeSteps && FootContactCrossed(from, to, .5f))
				{
					out_.brakeContacts |= 2;
					++brakeSteps_;
				}
			}
			else
			{
				phase_ += rate_ * dt;
				rate_ *= std::exp(-kCoastDecayRate * dt);
				if (speed <= kBrakeMinSpeed)
					strideRate_ = 0;
			}
		}
		phase_ = Wrap(phase_);
		out_.locked = locked_;
	}
	std::array<double, 4> DirectionWeights() const
	{
		const double sum = std::abs(forward_) + std::abs(right_);
		if (sum <= 1e-9)
			return { 1, 0, 0, 0 };
		// WalkForward, WalkBackward, WalkRight, WalkLeft, as the generator blends.
		return { std::max(forward_, 0.) / sum, std::max(-forward_, 0.) / sum, std::max(right_, 0.) / sum, std::max(-right_, 0.) / sum };
	}
	static double Sample(const std::array<float, kFootstepCameraBins>& curve, double phase)
	{
		const double x = Wrap(phase) * kFootstepCameraBins;
		const unsigned i = unsigned(x) % kFootstepCameraBins;
		const double t = x - std::floor(x);
		return curve[i] * (1 - t) + curve[(i + 1) % kFootstepCameraBins] * t;
	}
	FootstepCameraOutput out_;
	std::array<double, 3> posture_{};
	double phase_ = 0, rate_ = 0, lastEnginePhase_ = 0, gait_ = 0, ground_ = 1, lunge_ = -1, forward_ = 1, right_ = 0;
	double accel_ = 0, lastSpeed_ = 0, strideRate_ = 0;
	double lockAge_ = 0;
	std::array<double, 3> crossfade_{};
	bool reseek_ = false;
	unsigned brakeSteps_ = 0;
	bool locked_ = false, sprinting_ = false;
};

// Reference head motion for Classic: the display roll (strafe, turn rate, lean at speed) and the camera
// shake (movement vibration and mouse shake), in degrees. The shake changes the view only.
struct ReferenceHeadInput
{
	double yaw = 0, pitch = 0, dt = 0;
	bool paused = false;
	double strafe = 0, speedMetres = 0;
	bool aiming = false, leaning = false;
};
struct ReferenceHeadOutput
{
	double roll = 0;
	Rotation shake{};
};
class ClassicReferenceHead
{
public:
	ClassicReferenceHead() { Reset(); }
	ClassicReferenceHead(const ClassicReferenceHead&) = delete;
	ClassicReferenceHead& operator=(const ClassicReferenceHead&) = delete;
	void Reset()
	{
		camera_.Reset();
		previous_ = {};
		seeded_ = false;
		out_ = {};
		random_ = kShakeSeed;
		// The top 15 bits of a 32-bit linear congruential generator.
		const auto draw = [this]
		{
			random_ = random_ * 1664525u + 1013904223u;
			return random_ >> 17;
		};
		vibration_ = std::make_unique<MovementCameraVibration>(draw);
		mouse_ = std::make_unique<MouseShakeLayer>(draw);
		effects_ = std::make_unique<CameraEffects>(*vibration_, *mouse_);
	}
	const ReferenceHeadOutput& Update(const ReferenceHeadInput& in)
	{
		if (in.paused)
			return out_;
		if (!std::isfinite(in.yaw) || !std::isfinite(in.pitch) || !std::isfinite(in.dt) || in.dt <= 0 || in.dt > kClassicMaxFrameSeconds)
		{
			Reset();
			return out_;
		}
		const auto control = ClassicControlDegrees(in.yaw, in.pitch);
		if (!seeded_)
		{
			previous_ = control;
			camera_.SeedControl(control);
			seeded_ = true;
			return out_;
		}
		const auto look = ClassicLookInput(control, previous_);
		const double lookX = look[0], lookY = look[1];
		previous_ = control;
		const double dt = ClampFrameDelta(in.dt), speedCm = std::max(in.speedMetres, 0.) * kCentimetresPerMetre;
		const auto& state = camera_.Update(
			{ control, dt, lookX, lookY, std::clamp(in.strafe, -1., 1.), speedCm, in.aiming, in.leaning, in.aiming ? 2 : 1 });
		out_.roll = state.displayRoll;
		// Open-field shake intensity: speed and look input drive it, not the distance to a wall.
		effects_->PublishIntensity(false, 0);
		MovementVibrationInput vibration;
		vibration.speed = speedCm;
		vibration.yawRate = state.yawRate;
		vibration.pitchRate = state.pitchRate;
		effects_->Tick(dt, vibration, lookX, lookY);
		out_.shake = effects_->Present(CameraPov{}).rotation;
		return out_;
	}
	const ReferenceHeadOutput& Output() const { return out_; }

private:
	static constexpr std::uint32_t kShakeSeed = 0x174e5u;
	CameraMotion camera_;
	std::unique_ptr<MovementCameraVibration> vibration_;
	std::unique_ptr<MouseShakeLayer> mouse_;
	std::unique_ptr<CameraEffects> effects_;
	Rotation previous_{};
	std::uint32_t random_ = kShakeSeed;
	bool seeded_ = false;
	ReferenceHeadOutput out_;
};
}
