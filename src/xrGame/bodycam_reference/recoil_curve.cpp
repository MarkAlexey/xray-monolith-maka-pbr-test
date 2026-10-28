// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_curve.h"
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
float CubicBezier(float a, float b, float c, float d, float t)
{
	const float d0 = float(double(b) - a), d1 = float(double(c) - b), d2 = float(double(d) - c);
	const float l0 = float(double(float(double(d0) * t)) + a);
	const float l1 = float(double(float(double(d1) * t)) + b);
	const float l2 = float(double(float(double(d2) * t)) + c);
	const float u0 = float(double(float(double(l1) - l0)) + d0);
	const float u1 = float(double(float(double(l2) - l1)) + d1);
	const float v0 = float(double(float(double(u0) * t)) + a);
	const float v1 = float(double(float(double(u1) * t)) + b);
	return float(double(float(double(float(double(float(double(v1) - v0)) + u0)) * t)) + a);
}

RecoilCurve::RecoilCurve(std::vector<RecoilCurveKey> keys)
{
	if (keys.empty())
		throw std::invalid_argument("Empty recoil curve");
	for (unsigned i = 0; i < keys.size(); ++i)
	{
		if (i && keys[i].time <= keys[i - 1].time)
			throw std::invalid_argument("Recoil key times must increase");
	}
	keys_ = std::make_shared<const std::vector<RecoilCurveKey>>(std::move(keys));
}

float RecoilCurve::Sample(float t) const
{
	const auto& keys_ = *this->keys_;
	float value;
	if (keys_.size() == 1 || t < keys_.front().time)
		value = keys_.front().value;
	else if (t > keys_.back().time)
		value = keys_.back().value;
	else
	{
		unsigned i = 0;
		while (i + 2 < keys_.size() && t >= keys_[i + 1].time)
			++i;
		const auto& a = keys_[i];
		const auto& b = keys_[i + 1];
		const float span = float(double(b.time) - a.time);
		const float u = float(double(float(double(t) - a.time)) / span);
		const float third = float(1. / 3.);
		const float p1 = float(double(a.value) + float(double(float(double(span) * a.leave)) * third));
		const float p2 = float(double(b.value) - float(double(float(double(span) * b.arrive)) * third));
		value = CubicBezier(a.value, p1, p2, b.value, u);
	}
	return float(double(value) + 0.);
}
}
