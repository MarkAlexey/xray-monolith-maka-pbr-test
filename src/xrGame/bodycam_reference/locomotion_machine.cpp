#include "locomotion_machine.h"
#include <algorithm>
#include <utility>

namespace BodycamReference
{
float LocomotionTransitionDuration(double duration, double destinationWeight)
{
	const float u = 1.f - float(destinationWeight), u2 = u * u, u3 = u2 * u;
	const float scale = (u3 * (4.f / 3) - u2 * 2.f) + u * (5.f / 3);
	return std::max(float(duration), 0.f) * scale;
}
LocomotionMachine::LocomotionMachine(unsigned state, std::vector<float> weights, bool first)
	: LocomotionMachine(3, state, std::move(weights), first, 3)
{
}
LocomotionMachine::LocomotionMachine(unsigned count, unsigned state, std::vector<float> weights, bool first, unsigned limit)
	: stateCount_(count), currentState_(state), transitionLimit_(limit), firstUpdate_(first),
	  banks_{ std::vector<float>(count), std::move(weights) }
{
}
void LocomotionMachine::Reinitialize(unsigned initial)
{
	currentState_ = initial;
	records_.clear();
	firstUpdate_ = true;
}
float LocomotionMachine::PublicWeight(unsigned state) const
{
	return state < stateCount_ ? banks_[1 - writeBank_][state] : 0;
}
float LocomotionMachine::InternalWeight(unsigned state) const
{
	if (records_.empty())
		return currentState_ == state ? 1.f : 0.f;
	float weight = 0;
	for (unsigned i = 0; i < records_.size(); ++i)
	{
		const auto& r = records_[i];
		const float complement = 1.f - r.alpha;
		if (i == 0)
		{
			if (r.previous == state)
				weight = complement;
		}
		else
			weight *= complement;
		if (r.next == state)
			weight += r.alpha;
	}
	return std::clamp(weight, 0.f, 1.f);
}
std::optional<LocomotionTransitionChoice> LocomotionMachine::SelectTransition(bool move)
{
	if (currentState_ == 0)
		return move ? std::optional<LocomotionTransitionChoice>{ { 1, .3 } } : std::nullopt;
	if (currentState_ == 1)
	{
		if (move)
			return std::nullopt;
		return PublicWeight(1) == 1 ? LocomotionTransitionChoice{ 2, 0 } : LocomotionTransitionChoice{ 0, .2 };
	}
	return PublicWeight(2) == 1 ? std::optional<LocomotionTransitionChoice>{ { 0, .3 } } : std::nullopt;
}
float LocomotionMachine::Duration(const LocomotionTransitionChoice& choice, float weight) const
{
	return LocomotionTransitionDuration(choice.duration, weight);
}
LocomotionBlend LocomotionMachine::NewBlend(const LocomotionTransitionChoice&, float duration) const
{
	return MontageBlend(0, 1, duration, MontageBlendShape::Hermite);
}
float LocomotionMachine::StackAlpha(const LocomotionTransitionRecord& record) const
{
	return HermiteMontageAlpha(record.duration > 0 ? record.elapsed / record.duration : 0);
}
LocomotionMachineTrace LocomotionMachine::Update(double seconds, bool shouldMove, double parentWeight, bool activeBranch)
{
	const float dt = float(seconds), parent = float(parentWeight);
	LocomotionMachineTrace trace;
	if (recordDiagnostics_)
		trace.publishedBefore = banks_[1 - writeBank_];
	std::fill(banks_[writeBank_].begin(), banks_[writeBank_].end(), 0.f);
	pendingPublication_ = true;
	for (unsigned count = 0; count < transitionLimit_; ++count)
	{
		const auto choice = SelectTransition(shouldMove);
		if (!choice)
			break;
		const float oldWeight = InternalWeight(choice->next), duration = Duration(*choice, oldWeight);
		const unsigned previous = currentState_, id = nextRecordId_++;
		records_.push_back({ id, previous, choice->next, duration, 0, 0, true, NewBlend(*choice, duration) });
		currentState_ = choice->next;
		if (recordDiagnostics_)
			trace.started.push_back({ id, previous, currentState_, duration });
		if (!(oldWeight > 0))
			trace.initializedStates.push_back(currentState_);
		AfterStateChange(previous, currentState_, !(oldWeight > 0), oldWeight);
	}
	if (firstUpdate_)
	{
		if (recordDiagnostics_)
			for (const auto& r : records_)
				trace.skippedFirstRecords.push_back(r.id);
		records_.clear();
		firstUpdate_ = false;
	}
	std::vector<bool> visited(stateCount_, false);
	const auto visit = [&](unsigned state)
	{
		if (visited[state])
			return;
		visited[state] = true;
		trace.updates.push_back({ state, InternalWeight(state) });
	};
	for (auto& record : records_)
	{
		if (!record.active)
			continue;
		record.elapsed += dt;
		const bool complete = std::visit(
			[dt](auto& blend)
			{
				blend.Update(dt);
				return blend.Finished();
			},
			record.blend);
		record.alpha = StackAlpha(record);
		if (complete)
		{
			record.active = false;
			if (recordDiagnostics_)
				trace.completed.push_back(record.id);
		}
		else
		{
			visit(record.previous);
			visit(record.next);
		}
	}
	unsigned removeCount = 0;
	for (unsigned i = static_cast<unsigned>(records_.size()); i > 0; --i)
		if (!records_[i - 1].active)
		{
			removeCount = i;
			break;
		}
	if (recordDiagnostics_)
		for (unsigned i = 0; i < removeCount; ++i)
			trace.removed.push_back(records_[i].id);
	records_.erase(records_.begin(), records_.begin() + removeCount);
	if (records_.empty())
		visit(currentState_);
	for (unsigned state = 0; state < stateCount_; ++state)
	{
		const float weight = InternalWeight(state);
		if (weight > 0)
			banks_[writeBank_][state] = weight;
	}
	if (recordDiagnostics_)
		trace.currentWeights = banks_[writeBank_];
	trace.currentState = currentState_;
	for (const auto& update : trace.updates)
	{
		trace.contextUpdates.push_back({ update.state, update.weight * parent, update.state == currentState_ ? activeBranch : false });
		if (update.state == 1 || update.state == 2)
			trace.cyclesUpdated = true;
	}
	return trace;
}
bool LocomotionMachine::Publish()
{
	if (!pendingPublication_)
		return false;
	writeBank_ = 1 - writeBank_;
	pendingPublication_ = false;
	return true;
}
void LocomotionMachine::PrepareProxyUpdate()
{
	std::fill(banks_[writeBank_].begin(), banks_[writeBank_].end(), 0.f);
}
void LocomotionMachine::PublishProxyUpdate()
{
	writeBank_ = 1 - writeBank_;
	pendingPublication_ = false;
}
}
