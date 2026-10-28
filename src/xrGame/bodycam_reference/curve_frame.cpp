// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "curve_frame.h"
#include "constants.h"
#include <algorithm>

namespace BodycamReference
{
CurveInputs CurveFrame::ReadInputs() const
{
	const float mask = float(published_.maskSway.value_or(0)), enable = float(published_.enableAds.value_or(0)),
				action = float(published_.actionWeight.value_or(0));
	const double weight = 1.0 - mask;
	return { epoch_, mask, enable, action, std::clamp(float(weight), 0.f, 1.f), std::clamp(float(weight * (1.0 - enable)), 0.f, 1.f),
		float(1.0 - std::clamp(double(action), 0.0, 1.0)) };
}
void CurveFrame::CompleteEvaluation(double adsAlpha, double actualAdsAlpha, bool bonesValid, const FpCurves& curves)
{
	published_ = curves;
	published_.maskSway = float(curves.maskSway.value_or(0));
	published_.enableAds = float(actualAdsAlpha) > kBlendEpsilon && bonesValid ? float(adsAlpha) : 0;
	++epoch_;
}
float CurveFrame::ReadRecoilInput(bool ownerValid, bool instanceValid) const
{
	return ownerValid && instanceValid ? float(published_.enableAds.value_or(0)) : 0;
}
}
