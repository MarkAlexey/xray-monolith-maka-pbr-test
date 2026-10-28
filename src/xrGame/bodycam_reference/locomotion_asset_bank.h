#pragma once
#include "locomotion_frame.h"
#include "regional_pose.h"
#include "movement_additives.h"
namespace BodycamReference
{
// Immutable after loading. Keep this bank and its pose bank alive and stationary
// for the lifetime of every LocomotionFrame constructed from their assets.
class LocomotionAssetBank
{
public:
	static LocomotionAssetBank Load(const std::vector<std::uint8_t>&);
	const CyclePoseAssets& Cycle() const { return cycle_; }
	const UpperFrameAssets& Upper() const { return upper_; }
	const RegionalManifest& Regional() const { return regional_; }
	MovementWalkCurves Walking() const { return MovementWalkCurves(walking_); }
	GroundedLocomotionProperties Properties() const;
	GroundedRotationProperties Rotations() const { return GroundedRotationProperties(axes_); }

private:
	LocomotionAssetBank() = default;
	CyclePoseAssets cycle_;
	UpperFrameAssets upper_;
	RegionalManifest regional_;
	std::vector<WalkingCurve> walking_;
	std::vector<LocomotionPropertyCurve> properties_;
	std::array<std::vector<LocomotionCurveKey>, 4> axes_;
};
}
