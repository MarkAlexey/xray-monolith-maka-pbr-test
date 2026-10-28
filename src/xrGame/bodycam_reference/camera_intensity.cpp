// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "camera_intensity.h"
#include "constants.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr double kIntensityInterpSpeed = 10;
// Distance to the blocking hit, in centimetres, over which the intensities change.
constexpr double kNearHitCm = 350, kFarHitCm = 1500, kRegripNearHitCm = 1000;
constexpr double kNearHitShake = 2, kActionShake = .5, kFollowFarShake = 1;
constexpr float kOpenLocomotion = float(.3);
const char* const kCameraCurves[] = { "Camera_Pitch", "Camera_Yaw", "Camera_Roll" };
}
CameraIntensityTargets IntensityTargets(const CameraIntensityInput& input)
{
	if (!std::isfinite(input.distanceCm) || !std::isfinite(input.actionWeight) || !std::isfinite(input.followRotation) ||
		!std::isfinite(input.aimpointSource))
		throw std::invalid_argument("Camera intensity inputs must be finite");
	const double distance = float(input.distanceCm), base = float(input.baseRegrip);
	const double minimum =
		float(input.actionWeight) > 0 ? kActionShake : float(input.scopeActive ? input.minMagnifiedShake : input.minShake);
	double animation = input.blockingHit ? MapRangeClamped(distance, kNearHitCm, kFarHitCm, kNearHitShake, minimum) : minimum;
	if (float(input.followRotation) > .5)
		animation = MapRangeClamped(distance, kNearHitCm, kFarHitCm, kNearHitShake, kFollowFarShake);
	const double regrip = (input.aimpointSource == 1 || input.aimpointSource == 2)
		? (input.blockingHit ? MapRangeClamped(distance, kRegripNearHitCm, kFarHitCm, base, base / 2) : base / 2)
		: base;
	return { animation, regrip,
		input.blockingHit ? MapRangeClamped(distance, kNearHitCm, kFarHitCm, 1, kOpenLocomotion) : kOpenLocomotion };
}
CameraIntensity::CameraIntensity(double animation, double regrip)
	: animationScale_(float(animation)), regrip_(float(regrip)), targets_{ animationScale_, regrip_, kOpenLocomotion }
{
}
void CameraIntensity::Update(const CameraIntensityInput& input, double dt)
{
	targets_ = IntensityTargets(input);
	animationScale_ = float(FInterpTo(animationScale_, targets_.animationScale, dt, kIntensityInterpSpeed));
	regrip_ = float(FInterpTo(regrip_, targets_.regrip, dt, kIntensityInterpSpeed));
}
AnimationCurves CameraSlotCurves(const AnimationCurves& fire, double weight, const AnimationCurves& base)
{
	const float w = float(weight);
	if (w < 0 || w > 1)
		throw std::invalid_argument("Camera montage weight is outside [0, 1]");
	AnimationCurves output;
	for (const auto* name : kCameraCurves)
	{
		const float value = CurveValue(base, name);
		output[name] = w > kBlendEpsilon ? float(double(value) + double(float(double(w) * CurveValue(fire, name)))) : value;
	}
	return output;
}
Rotation CameraAnimationRotation(const AnimationCurves& curves, double scale, const Rotation& shake)
{
	const double fscale = float(scale);
	return { float(double(CurveValue(curves, kCameraCurves[0])) * fscale) + shake.pitch,
		float(double(CurveValue(curves, kCameraCurves[1])) * fscale) + shake.yaw,
		float(double(CurveValue(curves, kCameraCurves[2])) * fscale) + shake.roll };
}
}
