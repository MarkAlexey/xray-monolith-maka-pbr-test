#pragma once
#include "upper_frame.h"
namespace BodycamReference
{
struct LocomotionFrameResult
{
	UpperFrameResult upper;
	LocomotionPropertyState properties;
	LocomotionRotationState rotations;
	float sprintWeight = 0;
};
// Update producers -> rifle weight -> upper graph. The completed primary pose
// may replace curve publication before the following frame's producers run.
class LocomotionFrame
{
public:
	LocomotionFrame(
		const CyclePoseAssets&, const UpperFrameAssets&, const PoseAssetBank&, GroundedLocomotionProperties, GroundedRotationProperties);
	LocomotionFrameResult Update(
		double dt, const LocomotionEssential&, double aimingProgress, std::optional<double> aimYawRate = {}, bool recordDiagnostics = true);
	void PublishPrimaryCurves(NamedCurves);
	const NamedCurves& PublishedCurves() const { return upper_.PublishedCurves(); }
	const MovingRifleWeight& RifleWeight() const { return rifle_; }

private:
	LocomotionUpperFrame upper_;
	GroundedLocomotionProperties properties_;
	GroundedRotationProperties rotation_;
	MovingRifleWeight rifle_;
	std::int64_t frame_ = 0;
};
}
