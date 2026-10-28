#pragma once
#include "regrip_asset_bank.h"
#include "camera_intensity.h"
namespace BodycamReference
{
inline Rotation EvaluateRegripCamera(const RegripAssetBank& assets, const std::vector<RegripMontageSample>& samples, double alpha,
	double animationScale, const AnimationCurves& authored = {}, const Rotation& fire = {})
{
	const auto curves = BlendAnimationCurves(authored, assets.CameraCurves(samples, authored), alpha);
	return CameraAnimationRotation(curves, animationScale, fire);
}
}
