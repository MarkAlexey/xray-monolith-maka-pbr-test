#pragma once
#include "rifle_overlay.h"
namespace BodycamReference
{
struct LeftHandTargets
{
	Matrix gun, hand, pole;
};
// Left-hand targets from the C-clamp and aim poses of the physical bones as exported, before the
// translation retarget.
class RifleReferencePose
{
public:
	RifleReferencePose(std::vector<RifleTrack>, std::vector<LocalPose> cclamp, std::vector<LocalPose> aim);
	LeftHandTargets EvaluateLeft(double adsAlpha) const;
	void ApplyPhysicalLeft(std::vector<LocalPose>& physical, double adsAlpha, double attachWeight) const;
	const Matrix& ReferenceAppliedGun() const { return referenceAppliedGun_; }
	const Matrix& ReferenceCameraPoint() const { return referenceCamera_; }
	Matrix CurrentCameraPoint(const std::vector<LocalPose>& physical) const;

private:
	std::vector<RifleTrack> tracks_;
	std::vector<int> parents_;
	std::vector<LocalPose> cclamp_, aim_;
	std::array<unsigned, 16> left_;
	unsigned gun_, pole_, head_;
	Matrix referenceAppliedGun_, referenceCamera_;
};
}
