#pragma once
#include "blendspace_samples.h"
#include "locomotion_machine.h"
namespace BodycamReference
{
struct LeanSample
{
	unsigned sampleIndex;
	float weight, time = 0, historyDelta = 0, historyPreviousTime = 0;
	bool historyFlag = false;
};
struct LeanPlayerState
{
	BlendMarkerNode node;
	std::vector<LeanSample> samples;
	float blendWeight = 0, historyDelta = 0, previousTime = 0;
	bool relevanceFlag = false, markerFlag = false;
	std::array<float, 2> inputs{}, filteredInputs{};
};
struct LeanRegistration
{
	float weight, rootMotionWeight;
	std::array<float, 2> inputs;
};
struct LeanTick
{
	bool updated;
	float duration, previousRatio, currentRatio;
};
class LeanPlayer
{
public:
	explicit LeanPlayer(std::array<CookedGridPoint, 25> grid, LeanPlayerState initial = {});
	void Initialize(const std::function<void()>& base = {}, const std::function<void()>& exposed = {});
	LeanRegistration Register(
		double weight, double rootMotionWeight, double lr, double fb, const std::function<void(const LeanRegistration&)>& sink);
	LeanTick Tick(double dt, const LeanRegistration&);
	const LeanPlayerState& State() const { return state_; }

private:
	std::vector<LeanSample> Samples(double x, double y) const;
	std::array<CookedGridPoint, 25> grid_;
	LeanPlayerState state_;
};
class LeanAlpha
{
public:
	void Initialize(const std::function<void()>& base, const std::function<void()>& lean);
	float Update(double dt, const LocomotionStateUpdate& context, bool lod, bool ownerValid,
		const std::function<void(const LocomotionStateUpdate&)>& base, const std::function<void()>& exposed,
		const std::function<double()>& previousWeightGait, const std::function<void(const LocomotionStateUpdate&)>& lean);
	bool initialized = false;
	float cached = 0, actual = 0;
};
class FootRootAlpha
{
public:
	float Update(const LocomotionStateUpdate& context, bool lod, const std::function<void(const LocomotionStateUpdate&)>& child,
		const std::function<double()>& diagonal);
	float alpha = 1, actual = 0;
};
}
