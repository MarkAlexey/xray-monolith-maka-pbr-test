#include "regrip_pose.h"
#include <utility>

namespace BodycamReference
{
RegripPoseLayer::RegripPoseLayer(std::vector<int> parents, unsigned head)
	: parents_(std::move(parents)), count_(static_cast<unsigned>(parents_.size())), head_(head)
{
	parents_.insert(parents_.end(),
		{ static_cast<int>(head_), static_cast<int>(count_), static_cast<int>(count_), static_cast<int>(count_ + 1),
			static_cast<int>(count_ + 2) });
}
void RegripPoseLayer::Apply(
	std::vector<LocalPose>& physical, RegripTargets& targets, const std::vector<RegripSample>& samples, double alpha) const
{
	if (samples.empty() || alpha <= 0)
		return;
	auto& base = base_;
	auto& slot = slot_;
	auto& components = components_;
	base.assign(physical.begin(), physical.end());
	ComponentMatrices(base, parents_, components);
	base.push_back(Decompose(Multiply(InverseAffine(components[head_]), targets.gun)));
	base.push_back(Decompose(targets.handR));
	base.push_back(Decompose(targets.handL));
	base.push_back(Decompose(Multiply(InverseAffine(targets.handR), targets.poleR)));
	base.push_back(Decompose(targets.poleL));
	slot.assign(base.begin(), base.end());
	for (const auto& sample : samples)
	{
		AccumulateMeshSpacePose(slot, parents_, sample.deltas, sample.weight, scratch_, next_);
		slot.swap(next_);
	}
	for (unsigned i = 0; i < slot.size(); ++i)
		slot[i] = BlendAnimationLocal(base[i], slot[i], alpha);
	ComponentMatrices(slot, parents_, components);
	targets.gun = components[count_];
	targets.handR = Compose(slot[count_ + 1]);
	targets.handL = Compose(slot[count_ + 2]);
	targets.poleR = Multiply(targets.handR, Compose(slot[count_ + 3]));
	targets.poleL = Compose(slot[count_ + 4]);
	physical.assign(slot.begin(), slot.begin() + count_);
}
}
