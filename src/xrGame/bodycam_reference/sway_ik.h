#pragma once
#include "regrip_pose.h"
#include "skeleton_pose.h"
#include "spring.h"

namespace BodycamReference
{
struct SwayIkPose
{
	RegripTargets relations;
	Matrix headBeforeAimOffset;
};
struct SwayIkResult
{
	Matrix gun, handR, handL, poleR, poleL;
	ArmSolution right, left;
};
// Evaluate after regrip/recoil/movement additives, before late spine aim offset.
SwayIkPose ApplySwayAndCopyPoles(const SkeletonPose&, unsigned head, const ArmChain& right, const ArmChain& left, const RegripTargets&,
	const std::array<SwayNode, 10>&, const Vector& pivotCm, double lowerSwayCm);
// The caller evaluates the late spine pose between these two stages; the solved gun follows the changed head.
SwayIkResult TransportHeadAndSolveArms(
	SkeletonPose&, unsigned head, const ArmChain& right, const ArmChain& left, const SwayIkPose&, const Vector& leftElbowCm);
}
