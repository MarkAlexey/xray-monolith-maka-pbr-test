#include "skeleton_pose.h"
#include <functional>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
SkeletonPose::SkeletonPose(std::vector<int> parents, std::vector<LocalPose> locals)
	: parents_(std::move(parents)), locals_(std::move(locals)), components_(locals_.size())
{
	if (parents_.size() != locals_.size())
		throw std::invalid_argument("Skeleton parent/local count mismatch");
	for (int parent : parents_)
		if (parent < -1 || parent >= static_cast<int>(parents_.size()))
			throw std::invalid_argument("Skeleton parent index outside asset");
	std::vector<unsigned char> state(parents_.size());
	std::function<void(unsigned)> visit = [&](unsigned i)
	{
		if (state[i] == 2)
			return;
		if (state[i] == 1)
			throw std::invalid_argument("Cyclic skeleton ancestry");
		state[i] = 1;
		if (parents_[i] >= 0)
			visit(parents_[i]);
		order_.push_back(i);
		state[i] = 2;
	};
	for (unsigned i = 0; i < parents_.size(); ++i)
		visit(i);
}
void SkeletonPose::ResetLocals(const std::vector<LocalPose>& locals)
{
	if (locals.size() != locals_.size())
		throw std::invalid_argument("Authored pose count differs from bound skeleton");
	locals_ = locals;
	dirty_ = true;
}
void SkeletonPose::SetLocal(unsigned bone, const LocalPose& local)
{
	locals_[bone] = local;
	dirty_ = true;
}
void SkeletonPose::Evaluate() const
{
	if (!dirty_)
		return;
	for (unsigned bone : order_)
	{
		const auto local = Compose(locals_[bone]);
		components_[bone] = parents_[bone] < 0 ? local : Multiply(components_[parents_[bone]], local);
	}
	dirty_ = false;
}
const Matrix& SkeletonPose::Component(unsigned bone) const
{
	Evaluate();
	return components_[bone];
}
Matrix SkeletonPose::ParentComponent(unsigned bone) const
{
	return parents_[bone] < 0 ? kIdentityMatrix : Component(parents_[bone]);
}
void ValidateArmChain(const SkeletonPose& pose, const ArmChain& chain)
{
	if (chain.upper >= pose.Size() || chain.lower >= pose.Size() || chain.hand >= pose.Size())
		throw std::invalid_argument("Arm binding outside skeleton");
	const auto ancestor = [&](unsigned descendant, unsigned parent)
	{
		for (int i = pose.Parent(descendant); i >= 0; i = pose.Parent(i))
			if (i == static_cast<int>(parent))
				return true;
		return false;
	};
	if (!ancestor(chain.lower, chain.upper) || !ancestor(chain.hand, chain.lower))
		throw std::invalid_argument("Arm binding does not follow authored ancestry");
}
namespace
{
Vector Direction(const Vector& a, const Vector& b)
{
	return NormalizeArmVector({ b[0] - a[0], b[1] - a[1], b[2] - a[2] });
}
Vector TransformPoint(const Matrix& m, const Vector& p)
{
	const double w = 1 / (m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]);
	return { (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) * w, (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) * w,
		(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]) * w };
}
}
ArmSolution ApplyArmIK(SkeletonPose& pose, const ArmChain& chain, const Matrix& targetHand, const Matrix& targetPole)
{
	const auto target = Decompose(targetHand), pole = Decompose(targetPole);
	const auto upper = Decompose(pose.Component(chain.upper)), lower = Decompose(pose.Component(chain.lower));
	const auto hand = Decompose(pose.Component(chain.hand));
	const auto root = ReferenceToSource(upper.position), joint = ReferenceToSource(lower.position), end = ReferenceToSource(hand.position);
	const auto solved = SolveArm(root, ReferenceToSource(target.position), ReferenceToSource(pole.position), ArmSegmentLength(root, joint),
		ArmSegmentLength(joint, end));
	const auto upperDelta = SourceToReference(BetweenNormals(Direction(root, joint), Direction(root, solved.joint)));
	const auto lowerDelta = SourceToReference(BetweenNormals(Direction(joint, end), Direction(solved.joint, solved.end)));
	const auto upperRotation = NormalizeQuaternion(QuaternionProduct(upperDelta, upper.rotation));
	const auto lowerRotation = NormalizeQuaternion(QuaternionProduct(lowerDelta, lower.rotation));
	auto local = pose.Local(chain.upper);
	local.rotation =
		NormalizeQuaternion(QuaternionProduct(Conjugate(Decompose(pose.ParentComponent(chain.upper)).rotation), upperRotation));
	pose.SetLocal(chain.upper, local);
	const auto lowerParent = pose.ParentComponent(chain.lower);
	local = pose.Local(chain.lower);
	local.rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(Decompose(lowerParent).rotation), lowerRotation));
	local.position = TransformPoint(InverseAffine(lowerParent), SourceToReference(solved.joint));
	pose.SetLocal(chain.lower, local);
	const auto handParent = pose.ParentComponent(chain.hand);
	local = pose.Local(chain.hand);
	local.rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(Decompose(handParent).rotation), target.rotation));
	local.position = TransformPoint(InverseAffine(handParent), SourceToReference(solved.end));
	pose.SetLocal(chain.hand, local);
	return solved;
}
}
