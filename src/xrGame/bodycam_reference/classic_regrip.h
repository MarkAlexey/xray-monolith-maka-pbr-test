#pragma once
#include "camera_presentation.h"
#include "skeleton_pose.h"
#include "regrip_asset_bank.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace BodycamReference
{
// The regrip camera rotation as a matrix in the engine camera's local space. The neutral camera
// orientation is removed before the handedness changes.
inline Matrix ClassicRegripCameraDelta(const Rotation& rotation)
{
	LocalPose neutral, perturbed;
	neutral.rotation = WorldCameraQuaternion({});
	perturbed.rotation = WorldCameraQuaternion({}, rotation);
	const Matrix cameraBasis{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1 };
	const auto relative = Multiply(InverseAffine(Compose(neutral)), Compose(perturbed));
	return Multiply(Multiply(cameraBasis, relative), cameraBasis);
}

// Scales each clip's weight by its intensity setting. Timing and random selection are untouched,
// so a clip set to zero still plays, at no weight.
inline void ScaleClassicRegripSamples(
	const std::vector<RegripMontageSample>& samples, const float (&intensity)[4], std::vector<RegripMontageSample>& scaled)
{
	scaled.assign(samples.begin(), samples.end());
	for (auto& sample : scaled)
		sample.weight *= std::clamp(intensity[sample.clip], 0.f, 1.f);
}
inline std::vector<RegripMontageSample> ScaleClassicRegripSamples(
	const std::vector<RegripMontageSample>& samples, const float (&intensity)[4])
{
	std::vector<RegripMontageSample> scaled;
	ScaleClassicRegripSamples(samples, intensity, scaled);
	return scaled;
}

struct ClassicRegripInput
{
	std::uint32_t frame = 0;
	double seconds = 0;
	bool enabled = true, paused = false, aiming = false, magnified = false, firing = false;
	// An authored reload, check or inspect, item or NVG hands, or no usable rig.
	bool authoredOwnsHands = false;
};

// Runs the Reference regrip events and clip scheduler from Classic's aim, sight and fire state.
// Recreate it when the item or rig changes.
class ClassicRegrip
{
public:
	ClassicRegrip(const RegripAssetBank& assets, std::function<unsigned()> draw15) : random_(std::move(draw15)), assets_(assets) {}
	ClassicRegrip(const ClassicRegrip&) = delete;
	ClassicRegrip& operator=(const ClassicRegrip&) = delete;
	const std::vector<RegripMontageSample>& Update(const ClassicRegripInput& in)
	{
		if (!std::isfinite(in.seconds) || in.seconds < 0)
			throw std::invalid_argument("Classic regrip step must be finite and not negative");
		if (in.paused || (frame_ && *frame_ == in.frame))
			return samples_;
		frame_ = in.frame;
		worldTime_ += in.seconds;
		const bool blocked = !in.enabled || in.authoredOwnsHands;
		if (initialized_ && blocked && blocked_)
		{
			// Still blocked: follow the aim and sight state so that the release requests nothing.
			aiming_ = in.aiming;
			magnified_ = in.magnified;
			// A clip that was playing when an authored animation took the hands blends out at its own
			// rate; nothing new starts.
			if (!samples_.empty())
				samples_ = montages_->Update(in.seconds, false, false);
			return samples_;
		}
		if (initialized_ && blocked && in.enabled && !samples_.empty())
		{
			blocked_ = true;
			aiming_ = in.aiming;
			magnified_ = in.magnified;
			firing_ = false;
			montages_->StopAll(kRegripBlendSeconds);
			samples_ = montages_->Update(in.seconds, false, false);
			return samples_;
		}
		if (!initialized_ || blocked != blocked_)
		{
			// The first frame and every change of ownership start a fresh scheduler, which drops
			// the requests made before it. A clip still fading out keeps fading.
			Reset(in.aiming, in.magnified, !blocked);
			initialized_ = true;
			blocked_ = blocked;
			if (blocked)
				return samples_;
		}
		if (in.aiming != aiming_)
			montages_->Events().AimChanged(in.aiming);
		if (in.magnified != magnified_)
			montages_->Events().Request(kSightSwitchDelaySeconds);
		aiming_ = in.aiming;
		magnified_ = in.magnified;
		if (in.firing && !firing_)
			montages_->Semi().Begin(worldTime_);
		if (!in.firing && firing_)
			montages_->Semi().End(false);
		firing_ = in.firing;
		samples_ = montages_->Update(in.seconds, true, false);
		return samples_;
	}
	const std::vector<RegripMontageSample>& Samples() const { return samples_; }
	AnimationCurves CameraCurves() const { return assets_.CameraCurves(samples_); }
	std::uint64_t RandomDraws() const { return random_.Draws(); }

private:
	// A sight switch requests a regrip after this delay.
	static constexpr double kSightSwitchDelaySeconds = .1;
	void Reset(bool aiming, bool magnified, bool keepFading = false)
	{
		auto fresh = std::make_unique<RegripMontages>(assets_.Durations(), random_);
		if (keepFading && montages_ && !samples_.empty())
			fresh->TakeClips(*montages_);
		else
			samples_.clear();
		montages_ = std::move(fresh);
		montages_->Events().SeedAim(aiming);
		aiming_ = aiming;
		magnified_ = magnified;
		firing_ = false;
	}
	RecoilRandom random_;
	const RegripAssetBank& assets_;
	std::unique_ptr<RegripMontages> montages_;
	std::optional<std::uint32_t> frame_;
	bool initialized_ = false, aiming_ = false, magnified_ = false, firing_ = false, blocked_ = true;
	double worldTime_ = 0;
	std::vector<RegripMontageSample> samples_;
};

// Plays the regrip pose layer on the source rig's neutral pose and carries the resulting gun motion
// onto the engine's gun. The hand contacts stay as the engine posed them.
class ClassicReferenceRegripPose
{
public:
	ClassicReferenceRegripPose(const std::vector<int>& parents, std::vector<LocalPose> neutral, unsigned head,
		const Matrix& cameraFromSource, unsigned sourceGun, unsigned sourceWrist)
		: neutral_(std::move(neutral)), layer_(parents, head), cameraFromSource_(cameraFromSource)
	{
		const SkeletonPose rig(parents, neutral_);
		const auto gun = rig.Component(sourceGun), wrist = rig.Component(sourceWrist);
		sourceWristToGun_ = { gun[12] - wrist[12], gun[13] - wrist[13], gun[14] - wrist[14] };
	}
	// From the source mesh frame, where regrip and recoil are evaluated, to the space of engineCamera.
	Matrix EngineFromSource(const Matrix& engineCamera) const { return Multiply(engineCamera, cameraFromSource_); }
	// The gun transform moved to where the source rig holds its gun relative to the right wrist, so the
	// motion does not depend on where a mesh puts its attachment origin.
	Matrix VirtualGun(const RegripTargets& engineTargets, const Matrix& engineFromSource) const
	{
		const auto wrist = Multiply(engineTargets.gun, engineTargets.handR);
		auto virtualGun = engineTargets.gun;
		for (unsigned row = 0; row < 3; ++row)
			virtualGun[12 + row] = wrist[12 + row] + engineFromSource[row] * sourceWristToGun_[0] +
				engineFromSource[4 + row] * sourceWristToGun_[1] + engineFromSource[8 + row] * sourceWristToGun_[2];
		return virtualGun;
	}
	RegripTargets Apply(
		const RegripTargets& engineTargets, const std::vector<RegripSample>& samples, const Matrix& engineCamera, double alpha) const
	{
		if (samples.empty() || alpha <= 0)
			return engineTargets;
		const auto engineFromSource = EngineFromSource(engineCamera);
		const auto virtualGun = VirtualGun(engineTargets, engineFromSource);
		auto source = engineTargets;
		source.gun = Multiply(InverseAffine(engineFromSource), virtualGun);
		auto& physical = physical_;
		physical.assign(neutral_.begin(), neutral_.end());
		layer_.Apply(physical, source, samples, alpha);
		const auto animatedVirtualGun = Multiply(engineFromSource, source.gun);
		const auto motion = Multiply(animatedVirtualGun, InverseAffine(virtualGun));
		auto result = engineTargets;
		result.gun = Multiply(motion, engineTargets.gun);
		return result;
	}

private:
	std::vector<LocalPose> neutral_;
	mutable std::vector<LocalPose> physical_; // working copy of neutral_ for each Apply
	RegripPoseLayer layer_;
	Matrix cameraFromSource_;
	Vector sourceWristToGun_;
};

}
