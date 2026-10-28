#pragma once
#include "locomotion_machine.h"
namespace BodycamReference
{
struct DirectionCurveKey
{
	float value, arrive, leave;
};
class ChangeDirectionCurve
{
public:
	explicit ChangeDirectionCurve(std::array<DirectionCurveKey, 3> keys);
	float Sample(float time) const;
	float Alpha(float input) const;

private:
	std::array<DirectionCurveKey, 3> keys_;
};
struct DirectionalNotify
{
	unsigned notifyIndex, transitionIndex, previous, next;
};
struct DirectionalTrace
{
	LocomotionMachineTrace machine;
	std::vector<unsigned> selectedEdges;
	std::vector<DirectionalNotify> queuedNotifies;
};
class DirectionalMachine : public LocomotionMachine
{
public:
	explicit DirectionalMachine(
		ChangeDirectionCurve curve, unsigned state = 0, std::vector<float> weights = std::vector<float>(6), bool first = true);
	DirectionalTrace Update(double dt, unsigned direction, double hip, double feet, double parent = 1, bool active = true);

protected:
	std::optional<LocomotionTransitionChoice> SelectTransition(bool) override;
	float Duration(const LocomotionTransitionChoice&, float) const override;
	LocomotionBlend NewBlend(const LocomotionTransitionChoice&, float) const override;
	float StackAlpha(const LocomotionTransitionRecord&) const override;

private:
	ChangeDirectionCurve curve_;
	unsigned direction_ = 0;
	float hip_ = 0, feet_ = 0;
	std::vector<unsigned> selected_;
};
}
