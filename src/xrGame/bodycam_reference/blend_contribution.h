#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace BodycamReference
{
// Scalar contributions of X-Ray AnimationKeyCalculate.h::MixInterlerp.
// The caller supplies all active clips from ONE partition and channel, including
// idle. Raw blendAmount is an interpolation weight, not absolute pose influence:
// a lone EPS_S blend still supplies the entire authored pose.
inline std::vector<float> BlendContributions(const std::vector<float>& raw, float epsilon)
{
	std::vector<float> result(raw.size(), 0.f);
	if (raw.empty())
		return result;
	if (raw.size() == 1)
	{
		result[0] = 1.f;
		return result;
	}
	const auto fraction = [epsilon](float weight, float total)
	{ return std::abs(total) < epsilon ? 0.f : std::clamp(weight / total, 0.f, 1.f); };
	if (raw.size() == 2)
	{
		const float second = fraction(raw[1], raw[0] + raw[1]);
		result[0] = 1.f - second;
		result[1] = second;
		return result;
	}
	// The engine mixes in descending weight order against the running total; dividing by the final
	// total differs when an early sum is below EPS_S.
	std::vector<unsigned> order(raw.size());
	std::iota(order.begin(), order.end(), 0u);
	std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return raw[a] > raw[b]; });
	float total = raw[order[0]];
	result[order[0]] = 1.f;
	for (unsigned at = 1; at < order.size(); ++at)
	{
		const unsigned next = order[at];
		total += raw[next];
		const float weight = fraction(raw[next], total);
		for (unsigned prior = 0; prior < at; ++prior)
			result[order[prior]] *= 1.f - weight;
		result[next] = weight;
	}
	return result;
}
}
