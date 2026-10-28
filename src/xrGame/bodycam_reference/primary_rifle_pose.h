#pragma once
#include "rifle_local_pose.h"
#include "rifle_overlay.h"
#include "regional_pose.h"
namespace BodycamReference
{
struct PrimaryRifleInput
{
	bool hasUpdate = false, coherent = false;
	float amount = 0, weightGait = 0, basePoseClf = 0, sprintWeight = 0;
	const PoseCurveBundle* locomotion = nullptr;
	bool recordDiagnostics = true;
};
struct PrimaryRifleOutput
{
	bool applied = false, regional = false;
	std::vector<LocalPose> physical;
	AnimationCurves overlayCurves, publishedCurves;
	std::map<std::string, double> properties;
	std::optional<NamedCurves> primaryPublication;
};
NamedCurves ToNamedCurves(const AnimationCurves&);
using OverlayMontages = std::function<std::vector<LocalPose>(const std::vector<LocalPose>&)>;
using ActionCurvePublication = std::function<AnimationCurves(const AnimationCurves&)>;
// Takes the 89-bone pose and publishes the 68 physical bones. The caller supplies the montage and
// action curve stages.
class PrimaryRiflePose
{
public:
	PrimaryRiflePose(std::vector<RifleTrack>, unsigned spine, std::vector<LocalPose> retargetedCclamp, std::array<RiflePoseSample, 4>,
		RegionalManifest, PackedPose standingBase);
	PrimaryRiflePose(
		std::vector<RifleTrack>, RifleLocalPose, std::vector<LocalPose> retargetedCclamp, RegionalManifest, PackedPose standingBase);
	void BeginUpdate();
	void PublishCompletedCurves(AnimationCurves);
	PrimaryRifleOutput Apply(const PrimaryRifleInput&, const std::vector<LocalPose>& currentPhysical, const OverlayMontages& montages = {},
		const ActionCurvePublication& actionCurves = {}, const RegionalStage& stage = {});
	std::vector<Matrix> IncomingVirtualComponents(const std::vector<LocalPose>& currentPhysical) const;
	Matrix GunFromHand(const Matrix& hand, const Matrix& target, const Matrix& carriedRhs) const
	{
		return gun_.GunFromHand(hand, target, &carriedRhs);
	}
	const AnimationCurves& LayerInputs() const { return layerInputs_; }
	const AnimationCurves& PublishedCurves() const { return published_; }

private:
	std::vector<RifleTrack> tracks_;
	RifleLocalPose local_;
	RifleOverlayGun gun_;
	RegionalPose regional_;
	PackedPose base_;
	AnimationCurves rest_, published_, layerInputs_;
	std::optional<std::vector<LocalPose>> carried_;
};
}
