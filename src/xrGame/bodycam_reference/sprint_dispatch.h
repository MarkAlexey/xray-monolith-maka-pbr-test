#pragma once
#include <array>
#include <vector>
namespace BodycamReference
{
struct SprintPlayerEvent
{
	unsigned playerId;
	bool initialize;
	double weight = 0;
	bool activeBranch = false;
};
struct SprintDispatchTrace
{
	float rawAlpha, target, alpha;
	std::vector<SprintPlayerEvent> events;
};
class SprintTwoWayDispatch
{
public:
	std::array<SprintPlayerEvent, 2> Initialize();
	SprintDispatchTrace Update(double relativeAccelerationX, double dt, double parentWeight, bool activeBranch);
	bool Initialized() const { return initialized_; }
	float Cached() const { return cached_; }
	float Alpha() const { return alpha_; }
	const std::array<bool, 2>& Relevant() const { return relevant_; }

private:
	bool initialized_ = false;
	float cached_ = 0, alpha_ = 0;
	std::array<bool, 2> relevant_ = { false, false };
};
}
