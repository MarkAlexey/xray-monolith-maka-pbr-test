#pragma once
#include "blendspace_samples.h"
#include "blendspace_fir.h"
#include "compressed_curves.h"
#include <functional>
namespace BodycamReference
{
struct LocomotionPlayerState
{
	BlendMarkerNode node;
	std::vector<CachedBlendSample> samples;
	float historyDelta = 0, previousTime = 0;
	float blendWeight = 0;
	bool markerFlag = false, relevanceFlag = false;
};
struct LocomotionPlayerTick
{
	BlendMarkerResult markers;
	std::array<float, 2> filteredInputs;
	float weightedDuration;
};
class LocomotionPlayer
{
public:
	LocomotionPlayer(std::string assetIdentity, float startPosition, LocomotionGrid grid, std::array<float, 2> filterWindows,
		std::map<std::string, MarkerSequence> sequences, LocomotionPlayerState initial);
	void Initialize(const std::function<void(LocomotionPlayerState&)>& updateExposedInputs);
	LocomotionPlayerTick Tick(std::array<double, 2> processedInputs, double recordRate, CandidateContext context);
	NamedCurves Evaluate(const std::map<std::string, CompressedLocomotionCurves>& samplers,
		const std::map<std::string, std::set<std::string>>& admittedNames) const;
	LocomotionPlayerState& State() { return state_; }
	const LocomotionPlayerState& State() const { return state_; }
	const std::array<BlendSpaceFir, 2>& Filters() const { return filters_; }

private:
	std::string assetIdentity_;
	float startPosition_;
	LocomotionGrid grid_;
	std::array<BlendSpaceFir, 2> filters_;
	std::map<std::string, MarkerSequence> sequences_;
	std::array<std::optional<DurationSequence>, 4> durations_;
	LocomotionPlayerState state_;
};
}
