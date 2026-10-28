#include "directional_cache.h"
#include "constants.h"
#include "cycle_layer.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
void StateSource(const DirectionalCacheDispatch::StateRoot& hook, unsigned state, const std::function<void()>& source)
{
	if (hook)
		hook(state, source);
	else
		source();
}
}
std::array<float, 4> FourWayWeights(std::array<float, 4> desired)
{
	float sum = 0;
	for (float v : desired)
		sum += v;
	if (!(sum > kBlendEpsilon))
		return {};
	const float reciprocal = 1.f / sum;
	for (auto& v : desired)
		v = std::clamp(v * reciprocal, 0.f, 1.f);
	return desired;
}
DirectionalCacheDispatch::DirectionalCacheDispatch(std::array<unsigned, 8> order, StateRoot initialize, StateRoot update)
	: order_(order), initialize_(std::move(initialize)), update_(std::move(update))
{
	auto sorted = order;
	std::sort(sorted.begin(), sorted.end());
	const auto position = [&](unsigned id) { return std::find(order.begin(), order.end(), id); };
	if (sorted != std::array<unsigned, 8>{ 168, 169, 170, 171, 172, 173, kForwardCache, kSprintCache } ||
		position(kFirstDirectionCache) > position(kForwardCache) || position(kFirstDirectionCache) > position(kSprintCache))
		throw std::invalid_argument("Directional cache order must hold caches 168-173, 175 and 176, with 168 before 175 and 176");
}
std::vector<DirectionCacheEvent> DirectionalCacheDispatch::InitializeState(unsigned state, const std::function<bool(unsigned)>& resolve)
{
	std::vector<DirectionCacheEvent> events;
	StateSource(initialize_, state,
		[&]
		{
			multiWay_[state].actual = FourWayWeights(multiWay_[state].desired);
			std::function<void(unsigned)> request = [&](unsigned id)
			{
				const bool resolved = resolve(id);
				events.push_back({ DirectionCacheEventKind::CacheRequest, id, resolved });
				if (!resolved)
					return;
				if (id == kFirstDirectionCache)
				{
					events.push_back({ DirectionCacheEventKind::InitializeGait, kForwardGaitNode });
					forward_.InitializeMask(
						[&]
						{
							forward_.Gait().Initialize();
							// The mask's second input revisits the forward cache.
							request(kForwardCache);
							request(kSprintCache);
							request(kForwardCache);
						});
				}
				else
					events.push_back({ DirectionCacheEventKind::LinkedInput,
						id == kForwardCache		 ? 111u
							: id == kSprintCache ? 174u
												 : id - 57 });
			};
			for (unsigned direction : kStateDirections[state])
				request(kFirstDirectionCache + direction);
		});
	return events;
}
DirectionCacheTrace DirectionalCacheDispatch::Update(
	const std::vector<DirectionCacheContext>& contexts, std::array<float, 4> velocity, unsigned gait, double dt, double mask)
{
	DirectionCacheTrace trace;
	trace.weights = FourWayWeights(velocity);
	for (const auto& context : contexts)
		StateSource(update_, context.state,
			[&]
			{
				auto& node = multiWay_[context.state];
				node.desired = velocity;
				node.actual = trace.weights;
				for (unsigned i = 0; i < 4; ++i)
					if (trace.weights[i] > kBlendEpsilon)
					{
						auto c = context;
						c.weight *= trace.weights[i];
						trace.queues[kStateDirections[context.state][i]].push_back(c);
					}
			});
	for (unsigned i = 0; i < 6; ++i)
	{
		for (const auto& c : trace.queues[i])
			if (!trace.outerWinners[i] || c.weight > trace.outerWinners[i]->weight)
				trace.outerWinners[i] = c;
		trace.players[i] = trace.outerWinners[i];
	}
	trace.players[0].reset();
	if (trace.outerWinners[0])
	{
		const auto& outer = *trace.outerWinners[0];
		trace.forward = forward_.Update(gait, dt, outer.weight, mask, outer.activeBranch);
		for (unsigned i = 0; i < 2; ++i)
			if (trace.forward->winners[i])
			{
				const auto& winner = *trace.forward->winners[i];
				auto c = outer;
				c.weight = winner.weight;
				c.activeBranch = winner.activeBranch;
				c.forward = winner;
				trace.players[i == 0 ? 0 : 6] = c;
			}
	}
	return trace;
}
std::vector<DirectionPlayerUpdate> DirectionalCacheDispatch::OrderedPlayers(const DirectionCacheTrace& trace) const
{
	std::vector<DirectionPlayerUpdate> out;
	for (unsigned id : order_)
	{
		if (id == kFirstDirectionCache)
			continue;
		const unsigned index = id == kForwardCache ? 0 : id == kSprintCache ? 6 : id - kFirstDirectionCache;
		if (trace.players[index])
			out.push_back({ id, index < 6 ? kDirectionPlayerNodes[index] : kSprintBlendNode, *trace.players[index] });
	}
	return out;
}
}
