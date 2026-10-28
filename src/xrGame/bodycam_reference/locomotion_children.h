#pragma once
#include "normal_five_state.h"
#include <functional>

namespace BodycamReference
{
struct ChildStateDefinition
{
	bool conduit = false;
	std::vector<NormalStateLink> links;
};
struct ChildMachineDefinition
{
	std::array<ChildStateDefinition, 7> states;
	std::vector<NormalTransitionDefinition> transitions;
};
struct ChildTransition
{
	unsigned index;
	std::vector<unsigned> chain;
};
struct ChildMachineTrace
{
	LocomotionMachineTrace machine;
	std::vector<ChildTransition> selected;
	std::vector<float> inertialDurations;
};
struct DetailRuleInputs
{
	unsigned gait = 0;
	bool pivot = false;
	double mainGroundedWeight = 0, detailWeight = 0, publishedWeightGait = 0, stateElapsed = 0;
	std::array<double, 4> relevantRemaining{}; // States 1, 3, 4, 6.
};
std::array<bool, 12> DetailRuleResults(const DetailRuleInputs&);
class StopMachine : public LocomotionMachine
{
public:
	explicit StopMachine(ChildMachineDefinition);
	void Reinitialize(unsigned initialState = 0) override;
	ChildMachineTrace Update(double dt, double publishedFeet, double parentWeight = 1, bool activeBranch = true);

protected:
	std::optional<LocomotionTransitionChoice> SelectTransition(bool) override;

private:
	ChildMachineDefinition machine_;
	float feet_ = 0;
	std::vector<ChildTransition> selected_;
};
class DetailMachine : public LocomotionMachine
{
public:
	explicit DetailMachine(ChildMachineDefinition);
	void Reinitialize(unsigned initialState = 0) override;
	ChildMachineTrace Update(double dt, const DetailRuleInputs&, double parentWeight = 1, bool activeBranch = true);

protected:
	std::optional<LocomotionTransitionChoice> SelectTransition(bool) override;
	float Duration(const LocomotionTransitionChoice&, float destinationWeight) const override;

private:
	ChildMachineDefinition machine_;
	std::optional<LocomotionTransitionChoice> prepared_;
};
}
