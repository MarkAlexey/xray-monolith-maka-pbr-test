#include "sprint_dispatch.h"
#include "constants.h"
#include "cycle_layer.h"
#include <algorithm>
namespace BodycamReference
{
namespace
{
// Forward acceleration at which the sprint blend is fully on; it rises and falls at different rates.
constexpr float kFullSprintAcceleration = .25f, kRiseSpeed = 20.f, kFallSpeed = .5f;
}
std::array<SprintPlayerEvent, 2> SprintTwoWayDispatch::Initialize()
{
	initialized_ = false;
	relevant_ = { false, false };
	return { { { kSprintPlayerNodes[0], true }, { kSprintPlayerNodes[1], true } } };
}
SprintDispatchTrace SprintTwoWayDispatch::Update(double acceleration, double seconds, double parentWeight, bool active)
{
	const float input = float(acceleration), dt = float(seconds), parent = float(parentWeight);
	const float target = std::clamp(input / kFullSprintAcceleration, 0.f, 1.f);
	float result = target;
	if (initialized_)
	{
		const float speed = target >= cached_ ? kRiseSpeed : kFallSpeed, distance = target - cached_;
		if (!(distance * distance < 1e-8f))
			result = std::clamp(speed * dt, 0.f, 1.f) * distance + cached_;
	}
	cached_ = result;
	initialized_ = true;
	alpha_ = std::clamp(result, 0.f, 1.f);
	const std::array<bool, 2> relevant = { alpha_ < kBlendFull, kBlendEpsilon < alpha_ };
	SprintDispatchTrace trace{ input, target, alpha_, {} };
	for (unsigned i = 0; i < 2; ++i)
		if (relevant[i] && !relevant_[i])
			trace.events.push_back({ kSprintPlayerNodes[i], true });
	relevant_ = relevant;
	const bool both = relevant[0] && relevant[1];
	for (unsigned i = 0; i < 2; ++i)
		if (relevant[i])
		{
			const double weight = both ? double((i == 0 ? 1.f - alpha_ : alpha_) * parent) : parentWeight;
			trace.events.push_back({ kSprintPlayerNodes[i], false, weight, active });
		}
	return trace;
}
}
