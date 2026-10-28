#include "step_pose.h"
#include "constants.h"
#include "transform.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// Physical bones that take their translation from the skeleton; the others take it from the animation.
constexpr unsigned kSkeletonTranslatedBones = 59;
}
RifleTranslationRetarget::RifleTranslationRetarget(std::vector<Vector> reference, std::vector<int> modes)
{
	if (reference.size() != kSkeletonBoneCount || modes.size() != kTranslationModeCount)
		throw std::invalid_argument("Retarget needs 89 reference translations and 158 translation modes");
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
	{
		if (modes[i] != 0 && modes[i] != 1)
			throw std::invalid_argument("Translation mode must be 0 (animation) or 1 (skeleton)");
		if (modes[i] == 1)
			indices_.push_back(i);
	}
	if (indices_.size() != kSkeletonTranslatedBones || modes[0] != 0)
		throw std::invalid_argument("Retarget needs 59 skeleton-translated bones and an animation-translated root");
	for (const auto& p : reference)
	{
		for (double value : p)
			if (!std::isfinite(value))
				throw std::invalid_argument("Finite target reference required");
		translations_.push_back(SourceToReference(p));
	}
}
std::vector<LocalPose> RifleTranslationRetarget::Apply(std::vector<LocalPose> pose) const
{
	if (pose.size() != kPhysicalBoneCount && pose.size() != kSkeletonBoneCount)
		throw std::invalid_argument("Retarget pose must hold 68 or 89 bones");
	for (unsigned i : indices_)
		pose[i].position = translations_[i];
	return pose;
}
SequenceFrameTime FrameTime(double seconds, int numerator, int denominator)
{
	const double frames = (double(numerator) * seconds) / denominator;
	if (!std::isfinite(frames) || frames < -2147483646.0 || frames > 2147483646.0)
		throw std::invalid_argument("Supported finite frame time required");
	int frame = int(std::floor(frames));
	float subframe = float(frames - frame);
	const int carry = int(subframe);
	frame += carry;
	subframe -= float(carry);
	return { frame, subframe };
}
SequenceFramePosition SequenceFrameContext(double seconds, double length, int numerator, int denominator)
{
	if (!std::isfinite(length) || length <= 0)
		throw std::invalid_argument("Positive sequence duration required");
	const auto duration = FrameTime(float(length), numerator, denominator);
	const int frames = duration.frame + (duration.subframe >= .5f ? 1 : 0);
	if (frames <= 0)
		throw std::invalid_argument("Sequence must round to positive frame count");
	const auto time = FrameTime(seconds, numerator, denominator);
	return { time, frames, float(std::min(1.0, (double(time.frame) + time.subframe) / frames)) };
}
unsigned StepKeyIndex(double relative, unsigned count)
{
	const float value = float(relative);
	if (value <= 0 || count == 1)
		return 0;
	if (value >= 1)
		return count - 1;
	return unsigned(std::floor(float(count - 1) * value));
}
MovingRifleStepPose::MovingRifleStepPose(double duration, std::vector<StepPoseTrack> tracks)
	: duration_(duration), tracks_(std::move(tracks))
{
	SequenceFrameContext(0, duration_, 30, 1);
	for (const auto& track : tracks_)
	{
		if (track.positions.empty() || track.scales.empty() || track.rotations.empty())
			throw std::invalid_argument("Complete absolute STEP tracks required");
		for (const auto* vectors : { &track.positions, &track.scales })
			for (const auto& value : *vectors)
				for (double x : value)
					if (!std::isfinite(x))
						throw std::invalid_argument("Finite STEP vectors required");
		for (const auto& q : track.rotations)
		{
			double sum = 0;
			for (double x : q)
			{
				if (!std::isfinite(x))
					throw std::invalid_argument("Finite STEP quaternion required");
				sum += x * x;
			}
			if (sum == 0)
				throw std::invalid_argument("Nonzero STEP quaternion required");
		}
	}
}
std::vector<LocalPose> MovingRifleStepPose::SampleRelative(double relative) const
{
	std::vector<LocalPose> out;
	out.reserve(tracks_.size());
	for (const auto& track : tracks_)
	{
		const auto& p = track.positions[StepKeyIndex(relative, unsigned(track.positions.size()))];
		const auto& q = track.rotations[StepKeyIndex(relative, unsigned(track.rotations.size()))];
		const auto& s = track.scales[StepKeyIndex(relative, unsigned(track.scales.size()))];
		const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
		out.push_back({ SourceToReference(p), { -q[0] / norm, -q[2] / norm, -q[1] / norm, q[3] / norm }, { s[0], s[2], s[1] } });
	}
	return out;
}
std::vector<LocalPose> MovingRifleStepPose::SampleTime(double seconds, SequenceFramePosition* context) const
{
	const auto position = SequenceFrameContext(float(seconds), duration_, 30, 1);
	if (context)
		*context = position;
	return SampleRelative(position.relative);
}
}
