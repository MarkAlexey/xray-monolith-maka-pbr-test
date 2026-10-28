#pragma once
#include "cycle_layer.h"
#include "directional_machine.h"
#include "directional_cache.h"
#include "sprint_dispatch.h"
namespace BodycamReference
{
struct CycleProperties
{
	unsigned direction = 0, gait = 0;
	std::array<float, 4> velocity = {};
	double relativeAccelerationX = 0;
};
struct CyclePlayerEvent
{
	bool initialize;
	unsigned nodeId;
	std::optional<unsigned> sourceNode, cacheId;
	std::optional<CycleContext> context;
	std::optional<DirectionCacheContext> directionContext;
};
struct CycleGraphEvent
{
	std::string kind, reason;
	std::optional<CycleLayerEvent> layer;
	std::optional<DirectionCacheEvent> cache;
	std::optional<unsigned> state;
	std::optional<DirectionalTrace> directional;
	float alpha = 0;
};
struct CycleGraphTrace
{
	std::vector<CycleGraphEvent> events;
	std::vector<CyclePlayerEvent> players;
};
struct CycleGraphHooks
{
	std::function<bool(unsigned, const std::string&)> resolveCacheInitialize;
	std::function<void(const CycleTarget&)> refreshBarrier;
	std::function<void(const CyclePlayerEvent&)> onPlayer;
	DirectionalCacheDispatch::StateRoot initializeStateRoot, updateStateRoot;
};
class CycleDirectionalGraph
{
public:
	CycleDirectionalGraph(ChangeDirectionCurve, std::array<unsigned, 8> cacheOrder, CycleGraphHooks);
	CycleDirectionalGraph(const CycleDirectionalGraph&) = delete;
	CycleDirectionalGraph& operator=(const CycleDirectionalGraph&) = delete;
	CycleGraphTrace Initialize(CycleTarget, const std::function<GraphEpoch()>& source, CycleContext context = {});
	CycleGraphTrace Update(const std::function<CycleTarget()>& resolve, const std::function<GraphEpoch()>& source, CycleContext,
		const CycleProperties&, const AnimationCurves&, std::int64_t globalMarker, bool resetRelevance = true);
	DirectionalMachine& Directional() { return directional_; }
	DirectionEvaluationSnapshot EvaluationSnapshot() const { return cache_.EvaluationSnapshot(); }
	const SprintTwoWayDispatch& Sprint() const { return sprint_; }

private:
	CycleGraphTrace Run(const std::function<void()>&);
	void LayerEvent(const CycleLayerEvent&);
	void InitializeDirectional(const std::string&);
	void InitializeState(unsigned, const std::string&);
	void TargetUpdate(CycleContext);
	void Player(CyclePlayerEvent);
	void InputInitialize(unsigned, CycleContext);
	void InputUpdate(unsigned, CycleContext, std::optional<unsigned>, std::optional<DirectionCacheContext> = {});
	CycleGraphHooks hooks_;
	DirectionalMachine directional_;
	DirectionalCacheDispatch cache_;
	SprintTwoWayDispatch sprint_;
	OuterCycleLayer layer_;
	std::optional<GraphEpoch> lastTarget_;
	GraphEpoch sourceTuple_;
	bool firstTarget_ = true, resetRelevance_ = true;
	std::int64_t globalMarker_ = -1;
	const CycleProperties* properties_ = nullptr;
	const AnimationCurves* curves_ = nullptr;
	CycleGraphTrace* trace_ = nullptr;
};
}
