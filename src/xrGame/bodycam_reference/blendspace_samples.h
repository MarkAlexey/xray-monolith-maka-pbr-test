#pragma once
#include "locomotion_marker_tick.h"
namespace BodycamReference
{
struct GridSample
{
	unsigned sampleIndex;
	float weight, rateScale;
};
struct CookedGridPoint
{
	std::array<int, 3> indices;
	std::array<float, 3> weights;
};
struct GridSequence
{
	std::string identity;
	float rateScale = 1;
};
struct DurationSequence
{
	float length, rateScale;
};
class LocomotionGrid
{
public:
	LocomotionGrid(std::array<CookedGridPoint, 4>, std::array<GridSequence, 4>);
	std::vector<GridSample> Samples(double filteredX, double filteredY) const;
	const std::array<GridSequence, 4>& Sequences() const { return sequences_; }

private:
	std::array<CookedGridPoint, 4> points_;
	std::array<GridSequence, 4> sequences_;
};
struct CachedBlendSample
{
	unsigned sampleIndex;
	BlendMarkerSample marker;
};
std::vector<CachedBlendSample> RebuildLocomotionSamples(const std::vector<CachedBlendSample>& previous,
	const std::vector<GridSample>& samples, const std::array<GridSequence, 4>& sequences, bool markerEnabled);
float BlendSpaceDuration(const std::vector<GridSample>&, const std::array<std::optional<DurationSequence>, 4>&);
}
