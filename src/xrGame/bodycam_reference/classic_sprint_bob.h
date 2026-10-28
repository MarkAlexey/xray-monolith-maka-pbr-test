#pragma once
#include "footstep_camera_data.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace BodycamReference
{
// The Sprint Transition's gun step bob (docs/CLASSIC.md). The phase is the engine's footstep schedule;
// shape and size are the Reference step cycle the Footstep Camera replays, at the current speed. The
// blended walk/sprint shape is normalized to the interpolated swing so the size only grows with speed.
// The cycle has no rotation; the sway is derived from the translation with the ratios below.
struct SprintBobInput
{
	// A frame that does not follow the last one this bob ran (the other motion mode, another camera)
	// starts it afresh.
	std::uint32_t frame = 0;
	double dt = 0;
	bool paused = false;
	// Inside the sprint start, before the hand-off.
	bool starting = false;
	// Phase of the engine's step-sound schedule: left contact 0, right contact .5.
	std::optional<double> footPhase;
	double strideSeconds = 0; // One leg cycle at the current cadence; 0 when unknown.
	double speed = 0; // Planar metres/second on Reference gait speeds.
	double scale = 0; // The Gun Step Bob setting, 0..1: the share of the recorded step motion.
};
struct SprintBobOutput
{
	std::array<double, 3> offset{}; // Gun metres: right, up, forward.
	// Gun degrees about its grip: pitch (nose up), yaw (nose right), roll (right side down).
	std::array<double, 3> rotation{};
	double weight = 0;
	double phase = 0;
	bool active = false;
};

class ClassicSprintBob
{
public:
	void Reset() { *this = ClassicSprintBob{}; }
	const SprintBobOutput& Update(const SprintBobInput& in)
	{
		if (frame_ && in.frame != *frame_ && in.frame != *frame_ + 1)
			Reset();
		frame_ = in.frame;
		if (in.paused || !(in.dt > 0))
			return out_;
		if (in.strideSeconds > 0)
			stride_ = in.strideSeconds;
		const double dt = std::min(in.dt, kMaxStepSeconds);
		const bool stepping = in.footPhase && std::isfinite(*in.footPhase) && stride_ > 0;
		if (stepping)
			phase_ = *in.footPhase - std::floor(*in.footPhase);
		else if (stride_ > 0)
			phase_ = std::fmod(phase_ + dt / stride_, 1.); // Without a schedule the last cadence carries on.
		const double target = in.starting && stepping ? 1 : 0;
		const double step = stride_ > 0 ? dt * 2 / stride_ : 1;
		const double rise = 2 * step;
		const double speed = std::max(in.speed, 0.);
		const double walkTarget = std::min(speed / kFootstepWalkSpeed, 1.);
		const double sprintTarget = std::clamp((speed - kFootstepWalkSpeed) / (kFootstepSprintSpeed - kFootstepWalkSpeed), 0., 1.);
		if (weight_ <= 0)
		{
			walkW_ = walkTarget;
			sprintW_ = sprintTarget;
		}
		weight_ += std::clamp(target - weight_, -step, rise);
		walkW_ += std::clamp(walkTarget - walkW_, -step, step);
		sprintW_ += std::clamp(sprintTarget - sprintW_, -step, step);
		const double w = weight_ * weight_ * (3 - 2 * weight_);

		const auto& cycles = GunCycles();
		const auto& walk = cycles.curves[0];
		const auto& sprint = cycles.curves[1];
		for (unsigned a = 0; a < 3; ++a)
		{
			double lo = 0, hi = 0;
			for (unsigned i = 0; i < kFootstepCameraBins; ++i)
			{
				const double value = walk[a][i] * (1 - sprintW_) + sprint[a][i] * sprintW_;
				lo = i ? std::min(lo, value) : value;
				hi = i ? std::max(hi, value) : value;
			}
			const double swing = (cycles.swings[0][a] * (1 - sprintW_) + cycles.swings[1][a] * sprintW_) * walkW_;
			const double shape = Sample(walk[a], phase_) * (1 - sprintW_) + Sample(sprint[a], phase_) * sprintW_;
			out_.offset[a] = hi > lo ? shape * swing / (hi - lo) * w * in.scale : 0;
		}
		out_.rotation = { out_.offset[1] * kPitchPerMetre, out_.offset[0] * kYawPerMetre, -out_.offset[0] * kRollPerMetre };
		out_.weight = w;
		out_.phase = phase_;
		out_.active = weight_ > 0;
		return out_;
	}
	const SprintBobOutput& Output() const { return out_; }

private:
	static constexpr double kMaxStepSeconds = .1;
	// Gun sway per metre of bob: degrees of pitch per vertical metre, yaw and roll per lateral metre, and
	// the depth push per vertical metre.
	static constexpr double kPitchPerMetre = 100, kYawPerMetre = 80, kRollPerMetre = 200, kDepthFromVertical = .5;
	// The walk and the sprint cycle as gun motion (the depth axis takes its push from the vertical), and
	// their peak to peak per axis.
	using Curve = std::array<double, kFootstepCameraBins>;
	struct Cycles
	{
		std::array<std::array<Curve, 3>, 2> curves{};
		std::array<std::array<double, 3>, 2> swings{};
	};
	static const Cycles& GunCycles()
	{
		static const Cycles cycles = []
		{
			Cycles result;
			const FootstepCycle sources[2] = { FootstepCycle::WalkForward, FootstepCycle::SprintForward };
			for (unsigned c = 0; c < 2; ++c)
			{
				const auto& axes = FootstepCycles()[unsigned(sources[c])].axes;
				for (unsigned i = 0; i < kFootstepCameraBins; ++i)
				{
					result.curves[c][0][i] = axes[0][i];
					result.curves[c][1][i] = axes[1][i];
					result.curves[c][2][i] = axes[2][i] + axes[1][i] * kDepthFromVertical;
				}
				for (unsigned a = 0; a < 3; ++a)
				{
					const auto [lo, hi] = std::minmax_element(result.curves[c][a].begin(), result.curves[c][a].end());
					result.swings[c][a] = *hi - *lo;
				}
			}
			return result;
		}();
		return cycles;
	}
	static double Sample(const Curve& curve, double phase)
	{
		const double x = (phase - std::floor(phase)) * kFootstepCameraBins;
		const unsigned i = unsigned(x) % kFootstepCameraBins;
		const double t = x - std::floor(x);
		return curve[i] * (1 - t) + curve[(i + 1) % kFootstepCameraBins] * t;
	}
	SprintBobOutput out_;
	std::optional<std::uint32_t> frame_;
	double phase_ = 0, stride_ = 0, weight_ = 0, walkW_ = 0, sprintW_ = 0;
};
}
