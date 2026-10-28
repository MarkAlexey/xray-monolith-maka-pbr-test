#pragma once
#include "transform.h"
#include <vector>

namespace BodycamReference
{
// Indices are asset-owned; the parent-first order is resolved once.
class SkeletonPose
{
public:
	SkeletonPose(std::vector<int> parents, std::vector<LocalPose> locals);
	void ResetLocals(const std::vector<LocalPose>& locals);
	void SetLocal(unsigned bone, const LocalPose& local);
	const LocalPose& Local(unsigned bone) const { return locals_[bone]; }
	const Matrix& Component(unsigned bone) const;
	Matrix ParentComponent(unsigned bone) const;
	int Parent(unsigned bone) const { return parents_[bone]; }
	// Every bone after its parent.
	const std::vector<unsigned>& Order() const { return order_; }
	unsigned Size() const { return static_cast<unsigned>(locals_.size()); }

private:
	void Evaluate() const;
	std::vector<int> parents_;
	std::vector<unsigned> order_;
	std::vector<LocalPose> locals_;
	mutable std::vector<Matrix> components_;
	mutable bool dirty_ = true;
};
struct ArmChain
{
	unsigned upper, lower, hand;
};
void ValidateArmChain(const SkeletonPose&, const ArmChain&);
ArmSolution ApplyArmIK(SkeletonPose&, const ArmChain&, const Matrix& targetHand, const Matrix& targetPole);
}
