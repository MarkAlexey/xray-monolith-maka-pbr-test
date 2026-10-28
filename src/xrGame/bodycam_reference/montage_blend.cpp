// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "montage_blend.h"
#include "constants.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
float HermiteMontageAlpha(float alpha)
{
	if (alpha <= 0)
		return 0;
	if (alpha >= 1)
		return 1;
	return std::clamp((3.f - (alpha + alpha)) * (alpha * alpha), 0.f, 1.f);
}
float SinusoidalMontageAlpha(float alpha)
{
	const float angle = alpha * kPiF - kPiF / 2;
	const float sine = float(std::sin(double(angle)));
	return std::clamp((sine + 1) * .5f, 0.f, 1.f);
}
MontageBlend::MontageBlend(double begin, double end, double duration, MontageBlendShape shape) : shape_(shape)
{
	state_.value = float(begin);
	Reset(end, duration);
}
void MontageBlend::Reset(double end, double duration)
{
	auto& s = state_;
	s.begin = s.value;
	s.end = float(end);
	s.duration = float(std::max(duration, 0.));
	s.alpha = s.begin == s.end ? 1.f : 0.f;
	s.remaining = std::abs(1.f - s.alpha) * s.duration;
	s.target = (s.end - s.begin) + s.begin;
	if (s.duration == 0)
	{
		s.alpha = 1;
		s.value = s.target;
	}
}
float MontageBlend::Update(double seconds)
{
	const float dt = float(seconds);
	auto& s = state_;
	float leftover = 0;
	if (s.value != s.target)
	{
		const float old = s.remaining;
		if (old > dt)
		{
			s.remaining = old - dt;
			s.alpha = std::clamp(((1.f - s.alpha) / old) * dt + s.alpha, 0.f, 1.f);
		}
		else
		{
			s.remaining = 0;
			s.alpha = 1;
			leftover = dt - old;
		}
		if (shape_ == MontageBlendShape::Linear)
			s.value = (s.end - s.begin) * s.alpha + s.begin;
	}
	if (shape_ == MontageBlendShape::Sinusoidal)
		s.value = (s.end - s.begin) * SinusoidalMontageAlpha(s.alpha) + s.begin;
	else if (shape_ == MontageBlendShape::Hermite)
		s.value = (s.end - s.begin) * HermiteMontageAlpha(s.alpha) + s.begin;
	return leftover;
}
float AdditiveSlotScale(double totalNodeWeight)
{
	const float total = float(totalNodeWeight);
	return total > 1.00001f ? float(1. / total) : 1;
}
std::vector<float> AdditiveSlotWeights(const std::vector<float>& weights, double totalNodeWeight)
{
	const float reciprocal = AdditiveSlotScale(totalNodeWeight);
	std::vector<float> result;
	result.reserve(weights.size());
	for (float weight : weights)
		result.push_back(weight * reciprocal);
	return result;
}
}
