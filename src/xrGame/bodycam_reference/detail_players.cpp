#include "detail_players.h"
#include "constants.h"
#include "selection_sort.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
constexpr float kDetailPlayRate = 1.25f;
}
const std::array<unsigned, 4>& DetailPlayerOrder(unsigned state)
{
	static const std::map<unsigned, std::array<unsigned, 4>> order = { { 1, { 479, 478, 477, 476 } }, { 3, { 467, 466, 465, 464 } },
		{ 4, { 459, 458, 457, 456 } }, { 6, { 450, 449, 448, 447 } } };
	return order.at(state);
}
DetailPlayers::DetailPlayers()
{
	for (unsigned state : { 1, 3, 4, 6 })
	{
		weights_.emplace(state, WeightState{});
		for (unsigned id : DetailPlayerOrder(state))
		{
			RelevantSequenceSnapshot s;
			s.assetLength = 1.f;
			s.effectiveSignedRate = kDetailPlayRate;
			players_.emplace(id, DetailPlayerState{ s, false });
		}
	}
}
void DetailPlayers::InitializeState(unsigned state)
{
	auto& weights = weights_.at(state);
	weights.actual = FourWayWeights(weights.desired);
	const float time = state == 1 ? .1f : state == 6 ? .15f : .25f;
	for (unsigned id : DetailPlayerOrder(state))
	{
		auto& p = players_.at(id);
		p.sequence.accumulator = time;
		p.relevanceFlag = false;
	}
}
bool DetailPlayers::ChangeState(unsigned state)
{
	if (state == current_)
		return false;
	current_ = state;
	elapsed_ = 0;
	if (weights_.count(state))
		for (unsigned id : DetailPlayerOrder(state))
			players_.at(id).sequence.blendWeight = 0;
	return true;
}
void DetailPlayers::CompleteMachineUpdate(double dt)
{
	elapsed_ += float(dt);
}
DetailRuleInputs DetailPlayers::RuleInputs() const
{
	DetailRuleInputs out;
	out.stateElapsed = elapsed_;
	unsigned i = 0;
	for (unsigned state : { 1, 3, 4, 6 })
	{
		std::vector<std::optional<RelevantSequenceSnapshot>> ordered;
		for (unsigned id : DetailPlayerOrder(state))
			ordered.push_back(players_.at(id).sequence);
		out.relevantRemaining[i++] = RelevantSequenceRemaining(ordered);
	}
	return out;
}
const std::array<float, 4>& DetailPlayers::Weights(unsigned state) const
{
	return weights_.at(state).actual.value();
}
DetailPlayerDispatch DetailPlayers::Dispatch(const std::vector<LocomotionStateUpdate>& contexts, std::array<float, 4> velocity)
{
	DetailPlayerDispatch out;
	out.weights = FourWayWeights(velocity);
	for (const auto& context : contexts)
	{
		out.cyclesQueue.push_back(context);
		if (!weights_.count(context.state))
			continue;
		auto& node = weights_.at(context.state);
		node.desired = velocity;
		node.actual = out.weights;
		const auto& order = DetailPlayerOrder(context.state);
		for (unsigned i = 0; i < 4; ++i)
			if (out.weights[i] > kBlendEpsilon)
			{
				auto& p = players_.at(order[i]);
				const float weight = context.weight * out.weights[i];
				p.sequence.blendWeight = weight;
				p.relevanceFlag = p.relevanceFlag || weight >= kBlendFull;
				const unsigned group = context.state == 3 ? 1 : context.state == 4 ? 2 : 0;
				out.records.push_back({ group, order[i], weight });
			}
	}
	return out;
}
std::vector<DetailTickGroup> DetailPlayers::Tick(const std::vector<DetailSyncRecord>& records, double delta)
{
	const float dt = float(delta);
	std::vector<unsigned> groups;
	for (const auto& r : records)
		if (std::find(groups.begin(), groups.end(), r.group) == groups.end())
			groups.push_back(r.group);
	std::vector<DetailTickGroup> results;
	for (unsigned group : groups)
	{
		std::vector<std::pair<unsigned, DetailSyncRecord>> ordered;
		for (const auto& r : records)
			if (r.group == group)
				ordered.push_back({ unsigned(ordered.size()), r });
		SortByDescendingWeight(ordered, [](const auto& entry) { return entry.second.weight; });
		const auto leader = ordered[0].second.playerId;
		const float previous = players_.at(leader).sequence.accumulator, move = kDetailPlayRate * dt, unbounded = previous + move;
		const bool outside = unbounded < 0 || unbounded > 1;
		const float next = std::clamp(unbounded, 0.f, 1.f);
		DetailTickGroup result{ group, leader, previous, next, outside, {} };
		for (unsigned i = 0; i < ordered.size(); ++i)
		{
			const auto& r = ordered[i];
			auto& p = players_.at(r.second.playerId).sequence;
			const float movement = i == 0 ? move : next - previous;
			p.accumulator = next;
			p.history = SequenceDeltaHistory{ previous, movement, true };
			result.commits.push_back({ r.first, r.second.playerId, next, previous, movement });
		}
		results.push_back(std::move(result));
	}
	return results;
}
void MachineWeightPublication::Initialize(unsigned count)
{
	for (auto& bank : banks_)
		bank.assign(count, 0);
}
void MachineWeightPublication::Prepare()
{
	std::fill(banks_[current_].begin(), banks_[current_].end(), 0);
	pending_ = true;
}
void MachineWeightPublication::Write(unsigned index, double weight)
{
	banks_[current_].at(index) = float(weight);
}
float MachineWeightPublication::Read(unsigned index) const
{
	return banks_[1 - current_].at(index);
}
bool MachineWeightPublication::Publish()
{
	if (!pending_)
		return false;
	current_ = 1 - current_;
	pending_ = false;
	return true;
}
void PivotNotifyState::Notify(double speed, double limit)
{
	pivot_ = speed < limit;
	if (!remaining_)
		remaining_ = .1f;
}
bool PivotNotifyState::Tick(double dt)
{
	if (!remaining_)
		return false;
	*remaining_ -= float(dt);
	if (*remaining_ <= 0)
	{
		remaining_.reset();
		pivot_ = false;
		return true;
	}
	return false;
}
}
