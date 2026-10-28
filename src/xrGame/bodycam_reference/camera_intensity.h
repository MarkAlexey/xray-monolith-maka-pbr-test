#pragma once
#include "animation_blend.h"
#include "control.h"

namespace BodycamReference
{
struct CameraIntensityInput
{
	bool blockingHit = false, scopeActive = false;
	double distanceCm = 0, actionWeight = 0, followRotation = 0, aimpointSource = 0;
	double baseRegrip = 1, minShake = .5, minMagnifiedShake = 2;
};
struct CameraIntensityTargets
{
	double animationScale, regrip, locomotion;
};
CameraIntensityTargets IntensityTargets(const CameraIntensityInput&);
class CameraIntensity
{
public:
	CameraIntensity(double animationScale, double regrip);
	void Update(const CameraIntensityInput&, double dt);
	double AnimationScale() const { return animationScale_; }
	double Regrip() const { return regrip_; }
	double Locomotion() const { return targets_.locomotion; }
	const CameraIntensityTargets& Targets() const { return targets_; }

private:
	float animationScale_, regrip_;
	CameraIntensityTargets targets_;
};
AnimationCurves CameraSlotCurves(const AnimationCurves& fire, double weight, const AnimationCurves& base);
Rotation CameraAnimationRotation(const AnimationCurves&, double scale, const Rotation& shake = {});
}
