#pragma once
#include "locomotion_asset_bank.h"
#include "rifle_asset_bank.h"
#include "viewmodel_pose.h"
#include "idle_motion.h"
namespace BodycamReference
{
struct PrimaryVirtualPose
{
	Matrix gun, incomingGun, incomingHandR;
};
struct PhysicalHandPoints
{
	Matrix left, right;
};
// Bones the late graph reads, in the order of PrimaryFrame::lateBones_.
enum LateBone : unsigned
{
	kLateHandR,
	kLateVirtualGun,
	kLateVirtualHandR,
	kLateVirtualRhsGun,
	kLateHandL,
	kLateGun
};
// The 89-bone character animation graph. The asset banks must outlive it. Action poses enter at
// Apply; the late graph publishes the walking curves after recoil and before sway, spine and IK.
class PrimaryFrame
{
public:
	PrimaryFrame(const LocomotionAssetBank&, const PoseAssetBank&, const RifleAssetBank&, WalkingStressAdapter = {});
	const LocomotionFrameResult& Update(
		double dt, const LocomotionEssential&, double aimingProgress, bool aiming, std::optional<double> aimYawRate = {});
	PrimaryRifleOutput Apply(const std::vector<LocalPose>& physical, const OverlayMontages& montages = {},
		const ActionCurvePublication& actions = {}, const RegionalStage& stage = {});
	void PublishCompletedCurves(AnimationCurves);
	void CompleteWalking();
	const LocomotionFrameResult* LastLocomotion() const { return last_ ? &*last_ : nullptr; }
	void SetStressEnabled(bool enabled)
	{
		if (idleMotion_)
			idleMotion_->SetEnabled(enabled);
	}
	void EnableEngineMotion()
	{
		if (!idleMotion_)
		{
			idleMotion_.emplace();
			walking_.SetStressAdapter(idleMotion_->Adapter());
		}
	}
	const WalkingAdditiveState& Walking() const { return walking_.State(); }
	double ActionWeight() const { return actionWeight_; }
	PrimaryVirtualPose LateVirtualPose(const std::vector<LocalPose>& physical) const;
	// Fills the pose-derived fields of input; the caller sets aim point, motion, recoil and ground.
	void PrepareViewmodel(std::vector<LocalPose>& physical, ViewmodelPoseInput&) const;
	SwayIkResult EvaluateViewmodel(std::vector<LocalPose>& physical, ViewmodelPoseInput input);
	SwayIkResult EvaluateViewmodelInPlace(std::vector<LocalPose>& physical, ViewmodelPoseInput& input);
	const SkeletonPose& Skeleton() const { return viewmodel_.Skeleton(); }
	PhysicalHandPoints FinalHandPoints() const;
	Matrix PostOverlayGun() const { return postOverlayGun_; }
	void RecordPoseDiagnostics(bool enabled) { recordPoseDiagnostics_ = enabled; }
	const Matrix& ReferenceCameraPoint() const { return reference_.ReferenceCameraPoint(); }
	Matrix CameraPoint(const std::vector<LocalPose>& physical) const { return reference_.CurrentCameraPoint(physical); }
	const AnimationCurves& LayerInputs() const { return rifle_.LayerInputs(); }
	const AnimationCurves& PublishedCurves() const { return rifle_.PublishedCurves(); }
	std::vector<Matrix> IncomingVirtualComponents(const std::vector<LocalPose>& physical) const
	{
		return rifle_.IncomingVirtualComponents(physical);
	}

private:
	LocomotionFrame locomotion_;
	PrimaryRiflePose rifle_;
	RifleReferencePose reference_;
	ViewmodelPoseGraph viewmodel_;
	MovementAdditivesWalk walking_;
	std::optional<IdleMotion> idleMotion_;
	std::array<unsigned, 6> lateBones_; // indexed by LateBone
	Matrix postOverlayGun_{};
	bool recordPoseDiagnostics_ = true;
	std::optional<LocomotionFrameResult> last_;
	double delta_ = 0, actionWeight_ = 0;
};
}
