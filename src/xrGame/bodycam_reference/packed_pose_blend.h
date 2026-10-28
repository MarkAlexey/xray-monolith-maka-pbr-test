#pragma once
#include "pertrack_pose.h"
#include <functional>
namespace BodycamReference
{
using PackedTransform = std::array<double, 12>; // Qxyzw, Pxyz/pad, Sxyz/pad.
using PackedPose = std::vector<PackedTransform>;
struct WeightedPackedPose
{
	float weight;
	PackedPose pose;
};
struct TransitionWeights
{
	float outgoing, incoming;
};
PackedPose PackPose(const std::vector<SourceBoneTransform>&);
PackedPose NormalizePackedPose(PackedPose);
PackedTransform NormalizePackedTransform(PackedTransform);
PackedPose BlendSpacePackedPose(const std::vector<WeightedPackedPose>&);
PackedPose LeanPackedPose(const std::vector<WeightedPackedPose>&);
PackedPose TwoWayPackedPose(float alpha, bool aRelevant, bool bRelevant, const std::function<PackedPose(unsigned)>& child);
PackedPose GaitPackedPose(
	std::array<float, 2>, const std::function<PackedPose(unsigned)>& child, const std::function<PackedPose()>& reference);
PackedPose MultiWayPackedPose(
	std::array<float, 4>, const std::function<PackedPose(unsigned)>& child, const std::function<PackedPose()>& reference);
TransitionWeights WeightFactorPair(double alpha, double scale);
PackedTransform TransitionPackedTransform(const PackedTransform&, const PackedTransform&, TransitionWeights);
PackedPose TransitionPackedPose(const PackedPose& outgoing, const PackedPose& incoming, double alpha,
	const std::vector<int>& profileIndices, const std::vector<TransitionWeights>& profileWeights);
}
