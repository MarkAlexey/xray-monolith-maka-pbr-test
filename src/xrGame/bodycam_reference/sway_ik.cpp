#include "sway_ik.h"
#include "viewmodel_helpers.h"

namespace BodycamReference
{
SwayIkPose ApplySwayAndCopyPoles(const SkeletonPose& skeleton, unsigned head, const ArmChain& right, const ArmChain& left,
	const RegripTargets& input, const std::array<SwayNode, 10>& nodes, const Vector& pivot, double lowerSway)
{
	SwayIkPose output{ input, skeleton.Component(head) };
	auto& targets = output.relations;
	targets.gun = ApplyLowerSway(ApplySpringStack(targets.gun, output.headBeforeAimOffset, nodes, pivot), lowerSway);
	const auto copy = [&](const Matrix& handLocal, Matrix& poleLocal, const ArmChain& arm, bool rightSide)
	{
		const Matrix hand = Multiply(targets.gun, handLocal);
		const Matrix& storageParent = rightSide ? targets.gun : hand;
		const Matrix current = Multiply(storageParent, poleLocal);
		const Matrix result = CopySwayPole(current, skeleton.Component(arm.lower), hand);
		poleLocal = Multiply(InverseAffine(storageParent), result);
	};
	copy(targets.handR, targets.poleR, right, true);
	copy(targets.handL, targets.poleL, left, false);
	return output;
}
SwayIkResult TransportHeadAndSolveArms(
	SkeletonPose& skeleton, unsigned head, const ArmChain& right, const ArmChain& left, const SwayIkPose& input, const Vector& elbow)
{
	SwayIkResult output;
	const auto& relations = input.relations;
	const Matrix headDelta = Multiply(skeleton.Component(head), InverseAffine(input.headBeforeAimOffset));
	output.gun = Multiply(headDelta, relations.gun);
	output.handR = Multiply(output.gun, relations.handR);
	output.handL = Multiply(output.gun, relations.handL);
	output.poleR = Multiply(output.gun, relations.poleR);
	LocalPose offset;
	offset.position = SourceToReference(elbow);
	output.poleL = Multiply(Multiply(output.handL, relations.poleL), Compose(offset));
	output.right = ApplyArmIK(skeleton, right, output.handR, output.poleR);
	output.left = ApplyArmIK(skeleton, left, output.handL, output.poleL);
	return output;
}
}
