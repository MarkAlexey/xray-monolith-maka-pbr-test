#pragma once
#include "gait_blend.h"
namespace BodycamReference
{
// Direction caches are kFirstDirectionCache + direction (F, B, FL, BL, FR, BR). The F direction blends
// the gait (forward and sprint caches) with the forward cache through the sprint mask.
constexpr unsigned kFirstDirectionCache = 168, kForwardCache = 175, kSprintCache = 176, kForwardGaitNode = 177;
// The upper graph's cached Detail and Cycles poses.
constexpr int kDetailCacheNode = 597, kCyclesCacheNode = 598;
// The four directions each directional state blends.
constexpr std::array<std::array<unsigned, 4>, 6> kStateDirections{ { { 0, 1, 2, 4 }, { 0, 1, 3, 5 }, { 0, 1, 3, 4 }, { 0, 1, 2, 5 },
	{ 0, 1, 2, 5 }, { 0, 1, 3, 4 } } };
struct DirectionCacheContext
{
	unsigned state = 0;
	float weight = 0;
	bool activeBranch = false;
	double deltaTime = 0, rootMotionWeight = 1;
	std::optional<ForwardGaitContext> forward;
};
enum class DirectionCacheEventKind
{
	CacheRequest,
	InitializeGait,
	LinkedInput
};
struct DirectionCacheEvent
{
	DirectionCacheEventKind kind;
	unsigned id;
	bool accepted = false;
};
struct DirectionMultiWay
{
	std::array<float, 4> desired = {};
	std::optional<std::array<float, 4>> actual;
};
struct DirectionCacheTrace
{
	std::array<float, 4> weights;
	std::array<std::vector<DirectionCacheContext>, 6> queues;
	std::array<std::optional<DirectionCacheContext>, 6> outerWinners;
	std::array<std::optional<DirectionCacheContext>, 7> players;
	std::optional<ForwardGaitTrace> forward;
};
struct DirectionPlayerUpdate
{
	unsigned cacheId, nodeId;
	DirectionCacheContext context;
};
struct DirectionEvaluationSnapshot
{
	std::array<std::optional<std::array<float, 4>>, 6> multiWay;
	float maskAlpha;
	std::array<bool, 2> maskRelevant;
	std::array<float, 2> gaitWeights;
};
std::array<float, 4> FourWayWeights(std::array<float, 4> desired);
class DirectionalCacheDispatch
{
public:
	using StateRoot = std::function<void(unsigned, const std::function<void()>&)>;
	DirectionalCacheDispatch(std::array<unsigned, 8> cacheOrder, StateRoot initialize = {}, StateRoot update = {});
	std::vector<DirectionCacheEvent> InitializeState(unsigned state, const std::function<bool(unsigned)>& resolve);
	DirectionCacheTrace Update(
		const std::vector<DirectionCacheContext>& contexts, std::array<float, 4> velocity, unsigned gait, double dt, double publishedMask);
	std::vector<DirectionPlayerUpdate> OrderedPlayers(const DirectionCacheTrace&) const;
	const std::array<DirectionMultiWay, 6>& MultiWay() const { return multiWay_; }
	DirectionEvaluationSnapshot EvaluationSnapshot() const
	{
		DirectionEvaluationSnapshot out;
		for (unsigned i = 0; i < 6; ++i)
			out.multiWay[i] = multiWay_[i].actual;
		out.maskAlpha = forward_.MaskAlpha();
		out.maskRelevant = forward_.MaskRelevance();
		out.gaitWeights = forward_.Gait().Weights();
		return out;
	}

private:
	std::array<unsigned, 8> order_;
	StateRoot initialize_, update_;
	ForwardGaitDispatch forward_;
	std::array<DirectionMultiWay, 6> multiWay_;
};
}
