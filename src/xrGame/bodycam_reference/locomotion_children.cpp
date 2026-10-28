#include "locomotion_children.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
void Validate(const ChildMachineDefinition& m, bool detail)
{
	for (unsigned s = 0; s < 7; ++s)
		for (const auto& link : m.states[s].links)
		{
			if (link.transition >= m.transitions.size())
				throw std::invalid_argument("Child machine link refers to a missing transition");
			const auto& t = m.transitions[link.transition];
			if (t.previous != s || t.next >= 7 || !std::isfinite(t.duration) || link.automatic ||
				(detail ? link.transition >= 12 : (link.delegate < 103 || link.delegate > 106 || t.inertial)))
				throw std::invalid_argument("Child machine transition is not supported");
		}
}
std::optional<ChildTransition> Select(const ChildMachineDefinition& m, unsigned state,
	const std::function<bool(const NormalStateLink&)>& admit, std::array<bool, 7> seen = {})
{
	if (seen[state])
		return std::nullopt;
	seen[state] = true;
	for (const auto& link : m.states[state].links)
	{
		if (!admit(link))
			continue;
		const auto& t = m.transitions[link.transition];
		if (m.states[t.next].conduit)
		{
			auto leaf = Select(m, t.next, admit, seen);
			if (leaf)
			{
				leaf->chain.insert(leaf->chain.begin(), link.transition);
				return leaf;
			}
		}
		else
			return ChildTransition{ link.transition, { link.transition } };
	}
	return std::nullopt;
}
}
std::array<bool, 12> DetailRuleResults(const DetailRuleInputs& in)
{
	const bool running = in.gait == 1 || in.gait == 2, late = float(in.stateElapsed) > .1f;
	const float main = float(in.mainGroundedWeight), detail = float(in.detailWeight);
	return { running && main != 1, running && main == 1, float(in.relevantRemaining[0]) == 0, in.pivot,
		in.gait == 0 && float(in.publishedWeightGait) < 1.2f, in.pivot && late, float(in.relevantRemaining[1]) == 0,
		float(in.relevantRemaining[2]) == 0, in.pivot && late, detail != 1, detail == 1, float(in.relevantRemaining[3]) == 0 };
}
StopMachine::StopMachine(ChildMachineDefinition m) : LocomotionMachine(7, 0, std::vector<float>(7), false, 2), machine_(std::move(m))
{
	Validate(machine_, false);
}
void StopMachine::Reinitialize(unsigned initial)
{
	LocomotionMachine::Reinitialize(initial);
	firstUpdate_ = false;
}
std::optional<LocomotionTransitionChoice> StopMachine::SelectTransition(bool)
{
	const std::array<bool, 4> rules{ std::abs(feet_) < .5, std::abs(feet_) >= .5, feet_ > 0, feet_ < 0 };
	auto selected = Select(machine_, currentState_, [&](const auto& l) { return rules[l.delegate - 103] == l.desired; });
	if (!selected)
		return std::nullopt;
	const auto& t = machine_.transitions[selected->index];
	selected_.push_back(std::move(*selected));
	return LocomotionTransitionChoice{ t.next, t.duration };
}
ChildMachineTrace StopMachine::Update(double dt, double feet, double parent, bool active)
{
	feet_ = float(feet);
	selected_.clear();
	auto trace = LocomotionMachine::Update(dt, false, parent, active);
	return { std::move(trace), selected_, {} };
}
DetailMachine::DetailMachine(ChildMachineDefinition m) : LocomotionMachine(7, 0, std::vector<float>(7), false, 1), machine_(std::move(m))
{
	Validate(machine_, true);
}
void DetailMachine::Reinitialize(unsigned initial)
{
	if (machine_.states[initial].conduit)
		throw std::invalid_argument("Detail machine cannot start in a conduit state");
	LocomotionMachine::Reinitialize(initial);
	firstUpdate_ = false;
}
std::optional<LocomotionTransitionChoice> DetailMachine::SelectTransition(bool)
{
	const auto value = prepared_;
	prepared_.reset();
	return value;
}
float DetailMachine::Duration(const LocomotionTransitionChoice& c, float weight) const
{
	return c.inertial ? 0 : LocomotionMachine::Duration(c, weight);
}
ChildMachineTrace DetailMachine::Update(double dt, const DetailRuleInputs& inputs, double parent, bool active)
{
	const auto rules = DetailRuleResults(inputs);
	const auto selected = Select(machine_, currentState_, [&](const auto& l) { return rules[l.transition]; });
	prepared_.reset();
	std::vector<float> requests;
	if (selected)
	{
		const auto& t = machine_.transitions[selected->index];
		prepared_ = LocomotionTransitionChoice{ t.next, t.duration, t.inertial };
		if (t.inertial)
			requests.push_back(float(t.duration));
	}
	auto trace = LocomotionMachine::Update(dt, false, parent, active);
	return { std::move(trace), selected ? std::vector<ChildTransition>{ *selected } : std::vector<ChildTransition>{}, std::move(requests) };
}
}
