#pragma once
#include <cstdint>
#include <optional>

namespace BodycamReference
{
// The published first-person curves. A missing curve is not the same as zero.
struct FpCurves
{
	std::optional<double> maskSway, enableAds, actionWeight;
};
struct CurveInputs
{
	std::uint64_t epoch;
	float maskSway, enableAds, actionWeight, actualLook, actualMove, elbowAlpha;
};
class CurveFrame
{
public:
	void CompleteEvaluation(double adsAlpha, double actualAdsAlpha, bool bonesValid, const FpCurves& curves);
	CurveInputs ReadInputs() const;
	float ReadRecoilInput(bool ownerValid = true, bool instanceValid = true) const;
	const FpCurves& Published() const { return published_; }

private:
	FpCurves published_;
	std::uint64_t epoch_ = 0;
};
}
