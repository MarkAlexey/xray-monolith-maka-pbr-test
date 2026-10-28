#include "blendspace_samples.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace BodycamReference
{
LocomotionGrid::LocomotionGrid(std::array<CookedGridPoint, 4> points, std::array<GridSequence, 4> sequences)
	: points_(points), sequences_(std::move(sequences))
{
	std::set<std::string> identities;
	for (const auto& s : sequences_)
		if (s.identity.empty() || !identities.insert(s.identity).second || !std::isfinite(s.rateScale))
			throw std::invalid_argument("Grid sequences must be named, distinct and have a finite rate");
	for (const auto& point : points_)
		for (float w : point.weights)
			if (!std::isfinite(w))
				throw std::invalid_argument("Grid weights must be finite");
}
std::vector<GridSample> LocomotionGrid::Samples(double x, double y) const
{
	x = x <= 0 ? 0 : std::min(x, 1.0);
	y = y <= 0 ? 0 : std::min(y, 1.0);
	const int ix = int(x), iy = int(y);
	const double fx = x - ix, fy = y - iy;
	const std::array<float, 4> weights = { float((1 - fy) * (1 - fx)), float((1 - fy) * fx), float((1 - fx) * fy), float(fy * fx) };
	const std::array<int, 4> corners = { iy * 2 + ix, iy * 2 + ix + 1, (iy + 1) * 2 + ix, (iy + 1) * 2 + ix + 1 };
	std::vector<GridSample> samples;
	for (unsigned i = 0; i < 4; ++i)
	{
		const int index = corners[i];
		if (index < 0 || index >= 4)
			continue;
		const auto& point = points_[index];
		for (unsigned lane = 0; lane < 3; ++lane)
		{
			const int sample = point.indices[lane];
			if (sample < 0 || sample >= 4)
				continue;
			auto found =
				std::find_if(samples.begin(), samples.end(), [&](const GridSample& s) { return s.sampleIndex == unsigned(sample); });
			if (found == samples.end())
			{
				samples.push_back({ unsigned(sample), 0, sequences_[sample].rateScale });
				found = samples.end() - 1;
			}
			found->weight = weights[i] * point.weights[lane] + found->weight;
		}
	}
	// Descending by weight. The swaps decide the order of equal weights, so this is not a std::sort.
	for (unsigned end = unsigned(samples.size()); end > 1; --end)
	{
		unsigned least = 0;
		for (unsigned i = 1; i < end; ++i)
			if (samples[i].weight < samples[least].weight)
				least = i;
		std::swap(samples[least], samples[end - 1]);
	}
	float total = 0;
	unsigned count = 0;
	while (count < samples.size() && samples[count].weight >= kBlendEpsilon)
	{
		total += samples[count].weight;
		++count;
	}
	samples.resize(count);
	if (count)
	{
		const float reciprocal = 1.f / total;
		for (auto& s : samples)
			s.weight = reciprocal * s.weight;
	}
	return samples;
}
std::vector<CachedBlendSample> RebuildLocomotionSamples(const std::vector<CachedBlendSample>& previous,
	const std::vector<GridSample>& samples, const std::array<GridSequence, 4>& sequences, bool markerEnabled)
{
	std::vector<CachedBlendSample> out;
	for (const auto& s : samples)
	{
		BlendMarkerSample marker;
		marker.sequenceIdentity = sequences[s.sampleIndex].identity;
		marker.weight = s.weight;
		marker.rateScale = s.rateScale;
		out.push_back({ s.sampleIndex, std::move(marker) });
	}
	if (markerEnabled)
		for (const auto& old : previous)
			for (auto& next : out)
				if (old.marker.sequenceIdentity == next.marker.sequenceIdentity)
				{
					const auto& source = old.marker;
					next.marker.time = source.time;
					next.marker.previousTime = source.previousTime;
					next.marker.previous = source.previous;
					next.marker.next = source.next;
				}
	return out;
}
float BlendSpaceDuration(const std::vector<GridSample>& samples, const std::array<std::optional<DurationSequence>, 4>& sequences)
{
	float total = 0;
	for (const auto& s : samples)
	{
		if (s.sampleIndex >= 4 || !sequences[s.sampleIndex])
			continue;
		const auto& asset = *sequences[s.sampleIndex];
		if (!std::isfinite(asset.length) || asset.length < 0 || !std::isfinite(asset.rateScale) || !std::isfinite(s.rateScale) ||
			!std::isfinite(s.weight))
			throw std::invalid_argument("Duration sample is not finite");
		const float rate = asset.rateScale * s.rateScale, denominator = rate == 0 ? 1 : std::abs(rate),
					weight = std::clamp(s.weight, 0.f, 1.f);
		total += (asset.length / denominator) * weight;
	}
	return total;
}
}
