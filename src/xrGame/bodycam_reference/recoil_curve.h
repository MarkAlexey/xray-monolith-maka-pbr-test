#pragma once
#include <vector>
#include <memory>

namespace BodycamReference
{
// One cubic Bezier segment by de Casteljau steps, each rounded to float.
float CubicBezier(float a, float b, float c, float d, float t);
struct RecoilCurveKey
{
	float time, value, arrive, leave;
};
// Cubic keys with unweighted tangents and constant extrapolation.
class RecoilCurve
{
public:
	explicit RecoilCurve(std::vector<RecoilCurveKey> keys);
	float Sample(float time) const;
	float LastTime() const { return keys_->back().time; }

private:
	std::shared_ptr<const std::vector<RecoilCurveKey>> keys_;
};
}
