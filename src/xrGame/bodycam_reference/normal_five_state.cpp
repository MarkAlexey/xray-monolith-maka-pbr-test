#include "normal_five_state.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
// Ids of the source graph's transition rules; SelectTransition evaluates each.
constexpr int kFirstDelegate = 113, kLastDelegate = 118;
}
NormalStateMachine::NormalStateMachine(NormalMachineDefinition machine, std::function<void(const NormalStateChange&)> change)
	: LocomotionMachine(5, 0, std::vector<float>(5), true, 3), machine_(std::move(machine)), onStateChange_(std::move(change))
{
	for (unsigned state = 0; state < 5; ++state)
		for (const auto& link : machine_.states[state].links)
		{
			if (link.transition >= machine_.transitions.size())
				throw std::invalid_argument("Normal state link names a transition past the end");
			const auto& t = machine_.transitions[link.transition];
			if (t.previous != state || t.next >= 5 || !std::isfinite(t.duration) ||
				(!link.automatic && (link.delegate < kFirstDelegate || link.delegate > kLastDelegate)))
				throw std::invalid_argument("Normal transition starts elsewhere, has no target or duration, or an unknown delegate");
		}
}
std::optional<LocomotionTransitionChoice> NormalStateMachine::SelectTransition(bool move)
{
	for (const auto& link : machine_.states[currentState_].links)
	{
		const auto& t = machine_.transitions[link.transition];
		bool canTake;
		if (link.automatic)
			canTake = automatic_(currentState_, link, t);
		else
		{
			switch (link.delegate)
			{
			case 113: canTake = rotateLeft_; break;
			case 114: canTake = rotateRight_; break;
			case 115: canTake = move; break;
			case 116: canTake = !move && PublicWeight(1) == 1; break;
			case 117: canTake = !move; break;
			default: canTake = PublicWeight(2) == 1; break; // 118
			}
		}
		if (canTake != link.desired)
			continue;
		selected_.push_back(link.transition);
		if (t.inertial)
			requests_.push_back(static_cast<float>(t.duration));
		return LocomotionTransitionChoice{ t.next, t.duration, t.inertial };
	}
	return std::nullopt;
}
float NormalStateMachine::Duration(const LocomotionTransitionChoice& choice, float weight) const
{
	return choice.inertial ? 0 : LocomotionMachine::Duration(choice, weight);
}
void NormalStateMachine::AfterStateChange(unsigned previous, unsigned state, bool initialize, float weight)
{
	onStateChange_({ previous, state, initialize, weight, machine_.states[state].playerNodes });
}
NormalFiveStateTrace NormalStateMachine::Update(
	double dt, bool move, bool left, bool right, NormalAutomaticRule automaticRule, double parent, bool active)
{
	if (!automaticRule)
		throw std::invalid_argument("Normal machine update needs an automatic transition rule");
	rotateLeft_ = left;
	rotateRight_ = right;
	automatic_ = std::move(automaticRule);
	selected_.clear();
	requests_.clear();
	auto trace = LocomotionMachine::Update(dt, move, parent, active);
	automatic_ = {};
	return { std::move(trace), selected_, requests_ };
}
}
