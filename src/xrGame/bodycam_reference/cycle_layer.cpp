// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "cycle_layer.h"
namespace BodycamReference
{
bool OuterCycleLayer::Eligible(const CycleTarget& target)
{
	return target.valid && target.root;
}
void OuterCycleLayer::Emit(std::vector<CycleLayerEvent>& events, CycleLayerEvent event)
{
	events.push_back(event);
	hooks_.onEvent(event);
}
std::vector<CycleLayerEvent> OuterCycleLayer::Initialize(
	CycleTarget target, const std::function<GraphEpoch()>& source, CycleContext context)
{
	std::vector<CycleLayerEvent> events;
	if (Eligible(target))
	{
		Emit(events, { CycleLayerEventKind::RefreshBarrier, kCycleRootNode });
		hooks_.refreshBarrier(target);
		const auto tuple = source();
		Emit(events, { CycleLayerEventKind::CopyInitializeTuple, 0, tuple });
		Emit(events, { CycleLayerEventKind::TargetInitialize, kCycleRootNode, std::nullopt, context });
	}
	for (unsigned node : kDirectionPlayerNodes)
		Emit(events, { CycleLayerEventKind::InputInitialize, node, std::nullopt, context });
	Emit(events, { CycleLayerEventKind::InputInitialize, kSprintBlendNode, std::nullopt, context });
	return events;
}
std::vector<CycleLayerEvent> OuterCycleLayer::Update(
	const std::function<CycleTarget()>& resolve, const std::function<GraphEpoch()>& source, CycleContext context)
{
	std::vector<CycleLayerEvent> events;
	Emit(events, { CycleLayerEventKind::ExposedUpdate });
	if (hooks_.updateExposedInputs)
		hooks_.updateExposedInputs();
	if (Eligible(resolve()))
	{
		const auto tuple = source();
		Emit(events, { CycleLayerEventKind::CopyUpdateTuple, 0, tuple });
		Emit(events, { CycleLayerEventKind::CopyProperties });
		context.weight = float(context.weight);
		context.rootMotionWeight = float(context.rootMotionWeight);
		context.deltaTime = float(context.deltaTime);
		context.activeBranch = true;
		context.primaryProxy = true;
		Emit(events, { CycleLayerEventKind::TargetUpdate, kCycleRootNode, std::nullopt, context });
	}
	else
		Emit(events, { CycleLayerEventKind::InputUpdate, kDirectionPlayerNodes.front(), std::nullopt, context });
	Emit(events, { CycleLayerEventKind::ClearPending });
	return events;
}
}
