#pragma once
#include "locomotion_machine.h"
#include <functional>

namespace BodycamReference
{
struct NormalStateLink
{
	unsigned transition;
	int delegate;
	bool desired = true, automatic = false;
};
struct NormalStateDefinition
{
	std::vector<NormalStateLink> links;
	std::vector<unsigned> playerNodes;
};
struct NormalTransitionDefinition
{
	unsigned previous, next;
	double duration;
	bool inertial = false;
};
struct NormalMachineDefinition
{
	std::array<NormalStateDefinition, 5> states;
	std::vector<NormalTransitionDefinition> transitions;
};
struct NormalStateChange
{
	unsigned previous, state;
	bool initialize;
	float previousDestinationWeight;
	std::vector<unsigned> playerNodes;
};
using NormalAutomaticRule = std::function<bool(unsigned, const NormalStateLink&, const NormalTransitionDefinition&)>;
struct NormalFiveStateTrace
{
	LocomotionMachineTrace machine;
	std::vector<unsigned> selected;
	std::vector<float> inertialDurations; // durations of the inertial transitions started this update
};
class NormalStateMachine : public LocomotionMachine
{
public:
	NormalStateMachine(NormalMachineDefinition machine, std::function<void(const NormalStateChange&)> onStateChange);
	NormalFiveStateTrace Update(double dt, bool shouldMove, bool rotateLeft, bool rotateRight, NormalAutomaticRule automaticRule,
		double parentWeight, bool activeBranch);

protected:
	std::optional<LocomotionTransitionChoice> SelectTransition(bool shouldMove) override;
	float Duration(const LocomotionTransitionChoice&, float destinationWeight) const override;
	void AfterStateChange(unsigned previous, unsigned state, bool initialize, float previousWeight) override;

private:
	NormalMachineDefinition machine_;
	std::function<void(const NormalStateChange&)> onStateChange_;
	NormalAutomaticRule automatic_;
	bool rotateLeft_ = false, rotateRight_ = false;
	std::vector<unsigned> selected_;
	std::vector<float> requests_;
};
}
