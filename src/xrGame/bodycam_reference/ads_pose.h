#pragma once
#include "transform.h"

namespace BodycamReference
{
struct AdsPoseInput
{
	Matrix currentGun, referenceAppliedGun, referenceCameraPoint;
	Matrix currentCameraPoint, aimPointLocal, headParent;
	double alpha = 0;
	bool magnified = false;
};
Matrix BlendAdsTarget(const Matrix& current, const Matrix& target, const Matrix& headParent, double alpha);
Matrix SolveAdsPose(const AdsPoseInput&);
Matrix BlendTransform(const Matrix& from, const Matrix& to, double alpha);
Matrix ApplyComponentAdditive(const Matrix& current, const Matrix& offset);
class AimPointBlend
{
public:
	AimPointBlend(const Matrix& magnified, const Matrix& canted, bool useMagnified);
	const Matrix& Update(double deltaSeconds, bool useMagnified);
	void SetEquipment(const Matrix& magnified, const Matrix& canted)
	{
		magnified_ = magnified;
		canted_ = canted;
		equipmentChanged_ = true;
	}
	const Matrix& Current() const { return current_; }
	double Playback() const { return playback_; }
	double MagnifiedWeight() const { return magnifiedWeight_; }

private:
	Matrix magnified_, canted_, current_, from_{}, to_{};
	bool magnifiedMode_;
	bool initialized_ = false, equipmentChanged_ = false;
	double playback_ = 1;
	double magnifiedWeight_ = 1, fromMagnifiedWeight_ = 1;
};
}
