#pragma once
#include "step_pose.h"
#include <cstdint>
namespace BodycamReference
{
using FloatVector = std::array<float, 3>;
using FloatQuaternion = std::array<float, 4>;
struct SourceBoneTransform
{
	Vector p;
	Quaternion q;
	Vector s;
}; // source local basis, centimetres
struct PerTrackBoneKeys
{
	unsigned boneIndex;
	std::vector<FloatVector> translations;
	std::vector<FloatQuaternion> rotations;
	std::vector<unsigned> translationFrames, rotationFrames;
	FloatVector scale;
};
// Reciprocal square root seeds of the source runtime for lengths in [.25, 4), stored as 3-bit
// decrements between neighbouring float bit patterns.
class RsqrtSeedTable
{
public:
	explicit RsqrtSeedTable(const std::vector<std::uint8_t>& packedDrops);
	float Estimate(float length) const;

private:
	std::array<float, 16384> seeds_;
};
class PerTrackPose
{
public:
	enum class Policy
	{
		Locomotion,
		DetailAdditive,
		RotateReferenceRoot
	};
	// Decoded keyframes, not baked samples.
	PerTrackPose(double length, unsigned frames, std::vector<PerTrackBoneKeys> tracks, Policy policy = Policy::Locomotion);
	std::vector<SourceBoneTransform> Sample(double seconds, const std::vector<SourceBoneTransform>& initial,
		const std::vector<int>& skeletonToCompact, const std::vector<int>& translationModes, const RsqrtSeedTable&) const;

private:
	struct Selection
	{
		unsigned first, second;
		float alpha;
	};
	Selection Select(float relative, unsigned count, const std::vector<unsigned>& frames) const;
	double length_;
	unsigned frames_;
	Policy policy_;
	std::vector<PerTrackBoneKeys> tracks_;
};
}
