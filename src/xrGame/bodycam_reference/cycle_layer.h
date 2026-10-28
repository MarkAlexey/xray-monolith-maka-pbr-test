#pragma once
#include "upper_locomotion.h"
#include <array>
namespace BodycamReference
{
// Node ids of the source animation graph.
constexpr unsigned kCycleRootNode = 110;
constexpr unsigned kSprintBlendNode = 422;
constexpr std::array<unsigned, 6> kDirectionPlayerNodes{ 421, 429, 431, 428, 430, 427 };
constexpr std::array<unsigned, 2> kSprintPlayerNodes{ 424, 423 };
struct CycleTarget
{
	bool valid = false;
	std::optional<unsigned> root;
};
struct CycleContext
{
	double weight = 1, rootMotionWeight = 1, deltaTime = 0;
	bool activeBranch = false, primaryProxy = false;
};
enum class CycleLayerEventKind
{
	RefreshBarrier,
	CopyInitializeTuple,
	TargetInitialize,
	InputInitialize,
	ExposedUpdate,
	CopyUpdateTuple,
	CopyProperties,
	TargetUpdate,
	InputUpdate,
	ClearPending
};
struct CycleLayerEvent
{
	CycleLayerEventKind kind;
	unsigned node = 0;
	std::optional<GraphEpoch> tuple;
	std::optional<CycleContext> context;
};
struct CycleLayerHooks
{
	std::function<void(const CycleLayerEvent&)> onEvent;
	std::function<void(const CycleTarget&)> refreshBarrier;
	std::function<void()> updateExposedInputs;
};
class OuterCycleLayer
{
public:
	explicit OuterCycleLayer(CycleLayerHooks hooks) : hooks_(std::move(hooks)) {}
	std::vector<CycleLayerEvent> Initialize(CycleTarget, const std::function<GraphEpoch()>& sourceInit, CycleContext context = {});
	std::vector<CycleLayerEvent> Update(
		const std::function<CycleTarget()>& resolveTarget, const std::function<GraphEpoch()>& sourceUpdate, CycleContext);

private:
	static bool Eligible(const CycleTarget&);
	void Emit(std::vector<CycleLayerEvent>&, CycleLayerEvent);
	CycleLayerHooks hooks_;
};
}
