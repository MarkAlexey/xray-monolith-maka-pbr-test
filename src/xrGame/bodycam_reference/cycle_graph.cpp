#include "cycle_graph.h"
namespace BodycamReference
{
CycleDirectionalGraph::CycleDirectionalGraph(ChangeDirectionCurve curve, std::array<unsigned, 8> order, CycleGraphHooks hooks)
	: hooks_(std::move(hooks)), directional_(curve), cache_(order, hooks_.initializeStateRoot, hooks_.updateStateRoot),
	  layer_({ [this](const CycleLayerEvent& e) { LayerEvent(e); }, hooks_.refreshBarrier, {} })
{
}
CycleGraphTrace CycleDirectionalGraph::Run(const std::function<void()>& call)
{
	CycleGraphTrace trace;
	trace_ = &trace;
	call();
	trace_ = nullptr;
	return trace;
}
void CycleDirectionalGraph::Player(CyclePlayerEvent event)
{
	trace_->players.push_back(event);
	if (hooks_.onPlayer)
		hooks_.onPlayer(event);
}
void CycleDirectionalGraph::InitializeState(unsigned state, const std::string& reason)
{
	const auto events = cache_.InitializeState(state, [&](unsigned id) { return hooks_.resolveCacheInitialize(id, reason); });
	for (const auto& cache : events)
	{
		CycleGraphEvent e;
		e.kind = cache.kind == DirectionCacheEventKind::CacheRequest ? "cache-initialize-request"
			: cache.kind == DirectionCacheEventKind::InitializeGait	 ? "initialize-gait"
																	 : "linked-input-initialize-noop";
		e.reason = reason;
		e.cache = cache;
		e.state = state;
		trace_->events.push_back(std::move(e));
	}
}
void CycleDirectionalGraph::InitializeDirectional(const std::string& reason)
{
	firstTarget_ = true;
	directional_.Reinitialize();
	CycleGraphEvent e;
	e.kind = "directional-machine-initialize";
	e.reason = reason;
	trace_->events.push_back(std::move(e));
	InitializeState(0, reason);
}
void CycleDirectionalGraph::InputInitialize(unsigned node, CycleContext context)
{
	if (node == kSprintBlendNode)
		for (const auto& e : sprint_.Initialize())
			Player({ true, e.playerId, kSprintBlendNode, std::nullopt, context, {} });
	else
		Player({ true, node, std::nullopt, std::nullopt, context, {} });
}
void CycleDirectionalGraph::InputUpdate(
	unsigned node, CycleContext context, std::optional<unsigned> cache, std::optional<DirectionCacheContext> direction)
{
	if (node != kSprintBlendNode)
	{
		Player({ false, node, std::nullopt, cache, context, direction });
		return;
	}
	const auto result = sprint_.Update(properties_->relativeAccelerationX, context.deltaTime, context.weight, context.activeBranch);
	CycleGraphEvent e;
	e.kind = "sprint-two-way-update";
	e.alpha = result.alpha;
	trace_->events.push_back(std::move(e));
	for (const auto& child : result.events)
	{
		if (child.initialize)
			Player({ true, child.playerId, kSprintBlendNode, cache, {}, {} });
		else
		{
			auto c = context;
			c.weight = child.weight;
			auto d = direction;
			if (d)
				d->weight = float(child.weight);
			Player({ false, child.playerId, kSprintBlendNode, cache, c, d });
		}
	}
}
void CycleDirectionalGraph::TargetUpdate(CycleContext context)
{
	const auto current = sourceTuple_;
	if (!firstTarget_ && lastTarget_ && !CurrentOrPreviousGraphCounter(*lastTarget_, current, globalMarker_) && resetRelevance_)
		InitializeDirectional("directional-own-relevance-gap");
	lastTarget_ = current;
	firstTarget_ = false;
	const auto read = [&](const char* name)
	{
		const auto it = curves_->find(name);
		return it == curves_->end() ? 0 : it->second;
	};
	const auto result = directional_.Update(context.deltaTime, properties_->direction, read("HipOrientation_Bias"), read("Feet_Crossing"),
		context.weight, context.activeBranch);
	CycleGraphEvent e;
	e.kind = "directional-update";
	e.directional = result;
	trace_->events.push_back(std::move(e));
	for (unsigned state : result.machine.initializedStates)
		InitializeState(state, "directional-state-entry");
	std::vector<DirectionCacheContext> contexts;
	for (const auto& c : result.machine.contextUpdates)
		contexts.push_back({ c.state, c.weight, c.activeBranch, context.deltaTime, context.rootMotionWeight, {} });
	const auto dispatched = cache_.Update(contexts, properties_->velocity, properties_->gait, context.deltaTime, read("Mask_Sprint"));
	for (const auto& u : cache_.OrderedPlayers(dispatched))
	{
		auto c = context;
		c.weight = u.context.weight;
		c.activeBranch = u.context.activeBranch;
		InputUpdate(u.nodeId, c, u.cacheId, u.context);
	}
}
void CycleDirectionalGraph::LayerEvent(const CycleLayerEvent& layer)
{
	static const char* names[] = { "target-refresh-barrier", "copy-init-tuple", "target-initialize", "input-initialize",
		"exposed-input-update", "copy-update-tuple", "copy-target-properties", "target-update", "input-update", "clear-pending-blends" };
	CycleGraphEvent e;
	e.kind = names[unsigned(layer.kind)];
	e.layer = layer;
	trace_->events.push_back(std::move(e));
	switch (layer.kind)
	{
	case CycleLayerEventKind::CopyUpdateTuple: sourceTuple_ = *layer.tuple; break;
	case CycleLayerEventKind::TargetInitialize: InitializeDirectional("resolved-target-root-initialize"); break;
	case CycleLayerEventKind::TargetUpdate: TargetUpdate(*layer.context); break;
	case CycleLayerEventKind::InputInitialize: InputInitialize(layer.node, *layer.context); break;
	case CycleLayerEventKind::InputUpdate: InputUpdate(layer.node, *layer.context, {}); break;
	default: break;
	}
}
CycleGraphTrace CycleDirectionalGraph::Initialize(CycleTarget target, const std::function<GraphEpoch()>& source, CycleContext context)
{
	return Run([&] { layer_.Initialize(target, source, context); });
}
CycleGraphTrace CycleDirectionalGraph::Update(const std::function<CycleTarget()>& resolve, const std::function<GraphEpoch()>& source,
	CycleContext context, const CycleProperties& properties, const AnimationCurves& curves, std::int64_t marker, bool reset)
{
	properties_ = &properties;
	curves_ = &curves;
	globalMarker_ = marker;
	resetRelevance_ = reset;
	return Run([&] { layer_.Update(resolve, source, context); });
}
}
