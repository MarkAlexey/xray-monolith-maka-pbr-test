#pragma once
#include "ads_pose.h"
#include "flat_ground.h"
#include "recoil_pose.h"
#include "spine_pose.h"
#include "sway_ik.h"
#include <functional>

namespace BodycamReference
{
struct ViewmodelBinding
{
	ArmChain right, left;
	SpineChain spine;
	unsigned pelvis;
};
struct ViewmodelPoseInput
{
	Matrix gun, incomingVirtualHandR;
	Matrix evaluatorGunL, evaluatorHandL, evaluatorPoleL;
	Matrix referenceAppliedGun, referenceCameraPoint, currentCameraPoint, aimPointLocal;
	Vector actionPositionCm{}, pivotCm{ 0, 1, 0 }, elbowCm{};
	RecoilPose recoil;
	std::array<SwayNode, 10> springs{};
	AnimationCurves consumedMovementAdditives;
	std::vector<RegripSample> regripSamples;
	double actionWeight = 0, attachWeight = 1, adsAlpha = 0, regripAlpha = 0;
	double cachedAimPitch = 0, cachedAimYaw = 0, leanAlpha = 0, lowerSwayCm = 0;
	double disableIkOffset = 0, enableSpineRotation = 1, pelvisOffsetCm = 0, pelvisAlpha = 0;
	double engineActionPlacementWeight = 0;
	bool coherent = true, fullVirtual = true, magnified = false, applyMovementAdditives = false, applyFlatGround = false;
};
Matrix M4PoseOffset();
Matrix ApplyMovementAdditives(const Matrix&, const AnimationCurves&);
// Evaluate starts from the authored pose every time. The engine binding adapter maps the asset bones once.
class ViewmodelPoseGraph
{
public:
	ViewmodelPoseGraph(std::vector<int> parents, std::vector<LocalPose> initial, ViewmodelBinding binding);
	SwayIkResult Evaluate(
		const std::vector<LocalPose>& authored, const ViewmodelPoseInput&, const std::function<void()>& publishMovement = {});
	const SkeletonPose& Skeleton() const { return skeleton_; }

private:
	SkeletonPose skeleton_;
	RegripPoseLayer regrip_;
	ViewmodelBinding binding_;
};
}
