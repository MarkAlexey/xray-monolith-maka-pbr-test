#pragma once
#include "locomotion_children.h"
#include "animation_blend.h"
#include <cstdint>
#include <string>

namespace BodycamReference
{
// The graph counter wraps before 65535, which marks an epoch that never ran.
constexpr unsigned kNoGraphCounter = 65535;
inline unsigned NextGraphCounter(unsigned counter)
{
	const unsigned next = (counter + 1) & 65535;
	return next == kNoGraphCounter ? 0 : next;
}
struct GraphEpoch
{
	unsigned counter = kNoGraphCounter;
	std::int64_t frame = -1;
};
bool CurrentOrPreviousGraphCounter(GraphEpoch previous, GraphEpoch current, std::int64_t globalFrameMarker);
struct CachedLocomotionContext : LocomotionStateUpdate
{
	std::string source;
};
enum class UpperGraphEventKind
{
	InitializeMachine,
	InitializeCycles,
	CacheInitializeRequest,
	NormalPlayerReset,
	InitializeNormalState,
	NormalStateUpdate,
	StopStateUpdate,
	FlushCache,
	InitializeDetailState,
	DetailStateUpdate,
	UpdateCycles
};
struct UpperGraphEvent
{
	UpperGraphEventKind type{};
	unsigned machineIndex = 0;
	std::string kind, machine, reason;
	int cacheId = -1, state = -1;
	std::optional<bool> accepted;
	std::optional<CachedLocomotionContext> context;
	std::optional<NormalStateChange> change;
	GraphEpoch epoch;
	float deltaTime = 0;
};
struct NormalGraphInputs
{
	bool rotateLeft = false, rotateRight = false;
	NormalAutomaticRule automatic;
};
struct UpperGraphInput
{
	GraphEpoch epoch;
	std::int64_t globalFrameMarker = -1;
	double dt = 0;
	bool shouldMove = false, relevancyResetEnabled = true;
	std::optional<CachedLocomotionContext> normalContext;
	std::function<NormalGraphInputs()> normalInputs;
	std::function<DetailRuleInputs()> detailInputs;
	const AnimationCurves* priorCurves = nullptr;
	bool recordEvents = true;
};
struct UpperGraphTrace
{
	std::vector<UpperGraphEvent> events;
	std::vector<CachedLocomotionContext> detailQueue, cyclesQueue;
	std::optional<NormalFiveStateTrace> normal;
	std::vector<ChildMachineTrace> stop;
	std::optional<ChildMachineTrace> detail;
	std::optional<CachedLocomotionContext> detailWinner, cyclesWinner, outer417;
};
struct UpperGraphHooks
{
	std::function<bool(int, const std::string&, GraphEpoch)> resolveCacheInitialize;
	std::function<void(const UpperGraphEvent&)> onEvent;
	std::function<void(unsigned, const std::function<void()>&)> initializeStopState;
};
class UpperLocomotionGraph
{
public:
	UpperLocomotionGraph(NormalMachineDefinition normal, ChildMachineDefinition stop, ChildMachineDefinition detail, UpperGraphHooks hooks,
		std::vector<LocomotionMachine*> additionalOwners = {});
	UpperLocomotionGraph(const UpperLocomotionGraph&) = delete;
	UpperLocomotionGraph& operator=(const UpperLocomotionGraph&) = delete;
	UpperGraphTrace Update(const UpperGraphInput&);
	bool Publish();
	const NormalStateMachine& Normal() const { return normal_; }
	const StopMachine& Stop() const { return stop_; }
	const DetailMachine& Detail() const { return detail_; }

private:
	void OnNormalStateChange(const NormalStateChange&);
	std::function<void(const NormalStateChange&)> changeDuringUpdate_;
	UpperGraphHooks hooks_;
	NormalStateMachine normal_;
	StopMachine stop_;
	DetailMachine detail_;
	std::vector<LocomotionMachine*> owners_;
	std::array<std::optional<GraphEpoch>, 4> last_;
	std::array<bool, 4> first_{ { true, true, true, true } };
	bool pending_ = false;
};
}
