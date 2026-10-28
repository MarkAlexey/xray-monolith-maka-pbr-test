#pragma once
#include "directional_cache.h"
#include "locomotion_machine.h"

namespace BodycamReference
{
enum class StopAsset : unsigned
{
	F,
	B,
	LF,
	LB,
	RF,
	RB
};
struct StopEvaluator
{
	unsigned node;
	StopAsset asset;
	double time;
};
using StopEvaluatorSet = StopEvaluator[6];
const StopEvaluatorSet& StopPlantEvaluators(unsigned state);
struct StopEvaluatorUpdate
{
	StopEvaluator evaluator;
	float weight;
	bool activeBranch;
	std::optional<unsigned> child;
	bool outgoingZero = false;
};
enum class StopEventKind
{
	InitializeCache,
	InitializeMultiWay,
	InitializeEvaluator,
	UpdateEvaluator,
	UpdateCache
};
struct StopPlantEvent
{
	StopEventKind kind;
	unsigned state, node;
	std::optional<StopEvaluatorUpdate> evaluator;
	std::optional<LocomotionStateUpdate> context;
};
struct StopPlantSnapshot
{
	std::array<float, 4> weights;
	std::array<float, 2> left, right;
	bool layerRelevant;
};
struct StopPlantTrace
{
	std::array<float, 4> weights;
	std::vector<LocomotionStateUpdate> detailQueue;
	std::vector<StopEvaluatorUpdate> evaluators;
};
class StopPlantDispatch
{
public:
	using EventSink = std::function<void(const StopPlantEvent&)>;
	void InitializeState(unsigned state, const EventSink& sink = {});
	StopPlantTrace Update(const std::vector<LocomotionStateUpdate>& contexts, std::array<float, 4> velocity, unsigned hips, double dt,
		const EventSink& sink = {});
	StopPlantSnapshot EvaluationSnapshot(unsigned state) const;

private:
	struct Plant
	{
		GaitBlendList left{ GaitBlendList::Policy::LeftHip }, right{ GaitBlendList::Policy::RightHip };
		std::array<float, 4> desired{};
		std::optional<std::array<float, 4>> weights;
		bool layerRelevant = false;
	};
	std::array<Plant, 2> plants_;
};
}
