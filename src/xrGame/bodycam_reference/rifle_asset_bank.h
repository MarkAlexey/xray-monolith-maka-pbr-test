#pragma once
#include "rifle_local_pose.h"
#include "rifle_overlay.h"
#include "primary_rifle_pose.h"
#include "rifle_reference_pose.h"
#include "viewmodel_pose.h"
namespace BodycamReference
{
class RifleAssetBank
{
public:
	static RifleAssetBank Load(const std::vector<std::uint8_t>&);
	RifleLocalPose CreateLocal() const;
	RifleReferencePose CreateReference() const;
	ViewmodelPoseGraph CreateViewmodel() const;
	PrimaryRiflePose CreatePrimary(RegionalManifest, PackedPose standingBase) const;
	const std::vector<RifleTrack>& Tracks() const { return tracks_; }
	std::vector<LocalPose> RetargetedCclamp() const { return retarget_->Apply(cclamp_); }
	std::vector<LocalPose> RetargetAction(std::vector<LocalPose> pose) const { return retarget_->Apply(std::move(pose)); }

private:
	RifleAssetBank() = default;
	std::vector<RifleTrack> tracks_;
	std::optional<MovingRifleStepPose> step_;
	std::optional<RifleTranslationRetarget> retarget_;
	std::vector<LocalPose> cclamp_;
	std::vector<LocalPose> aim_;
	std::vector<RifleFoldedCurve> curves_;
	unsigned spine_ = 0;
};
}
