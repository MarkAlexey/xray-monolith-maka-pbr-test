#pragma once
#include "animation_blend.h"

namespace BodycamReference
{
struct RegripTargets
{
	Matrix gun, handR, handL, poleR, poleL;
};
struct RegripSample
{
	std::vector<AdditivePose> deltas;
	double weight = 0;
};
// Applies regrip samples to the physical bones and five virtual bones appended after them: the gun
// under the head, both hands under the gun, and the elbow poles under their hands.
class RegripPoseLayer
{
public:
	RegripPoseLayer(std::vector<int> physicalParents, unsigned head);
	void Apply(std::vector<LocalPose>& physical, RegripTargets& targets, const std::vector<RegripSample>& samples, double alpha) const;

private:
	std::vector<int> parents_;
	unsigned count_, head_;
	// Per-frame working poses, kept so that playing a clip does not allocate.
	mutable std::vector<LocalPose> base_, slot_, next_;
	mutable std::vector<Matrix> components_;
	mutable MeshSpaceScratch scratch_;
};
}
