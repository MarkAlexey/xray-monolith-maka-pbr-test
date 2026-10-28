// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_controller.h"
#include "recoil_random.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
namespace
{
// Targets and the accumulated mouse compensation stay within this many degrees.
constexpr double kLimitDegrees = 90;
// Compensation below this many degrees counts as none.
constexpr float kNoCompensation = float(1e-8);
// Targets recover to zero at once when their squared length falls below this.
constexpr float kRecoveredSquared = float(1e-4);
}

void ControllerRecoil::AccumulateMouse(double x, double y)
{
	if (!state_.firingActive)
		return;
	state_.inputCompensation[0] = std::clamp(state_.inputCompensation[0] + x, -kLimitDegrees, kLimitDegrees);
	state_.inputCompensation[1] = std::clamp(state_.inputCompensation[1] + y, -kLimitDegrees, kLimitDegrees);
}

void ControllerRecoil::Fire(const std::array<double, 2>& randomUnits)
{
	if (!state_.firingActive)
		state_.inputCompensation = {};
	state_.firingActive = true;
	for (unsigned i = 0; i < 2; ++i)
	{
		const float lo = float(double(float(config_.step[i][0])) * config_.scale[i]);
		const float hi = float(double(float(config_.step[i][1])) * config_.scale[i]);
		const float unit = float(std::floor(randomUnits[i] * (kRandomDrawMax + 1.)) * float(1. / kRandomDrawMax));
		const float sample = float(double(float(double(unit) * float(double(hi) - lo))) + lo);
		state_.target[i] = std::clamp(state_.target[i] + (i == 0 ? sample : -sample), -kLimitDegrees, kLimitDegrees);
	}
}

void ControllerRecoil::Stop()
{
	state_.firingActive = false;
	const float c = float(std::clamp(config_.compensation, 0., 1.));
	for (unsigned i = 0; i < 2; ++i)
	{
		const float input = float(double(c) * state_.inputCompensation[i] * (i == 0 ? 1 : -1));
		const float current = float(state_.current[i]);
		float keep = 1;
		if (std::abs(input) > kNoCompensation && float(double(current) * input) <= 0)
			keep = float(1. - std::clamp(double(std::abs(float(double(input) / current))), 0., 1.));
		state_.current[i] *= keep;
		state_.target[i] = state_.current[i];
	}
}

void ControllerRecoil::Update(double worldDelta)
{
	const auto previous = state_.current;
	for (unsigned i = 0; i < 2; ++i)
		state_.current[i] = FInterpTo(state_.current[i], state_.target[i], worldDelta, config_.smoothing[i]);
	// The targets decay only after current has followed them.
	if (!state_.firingActive)
	{
		const double squared = state_.target[0] * state_.target[0] + state_.target[1] * state_.target[1];
		const double decay = config_.damping <= 0 || squared < kRecoveredSquared ? 0 : 1 - std::clamp(worldDelta * config_.damping, 0., 1.);
		for (auto& target : state_.target)
			target = decay == 0 ? 0 : target * decay;
	}
	for (unsigned i = 0; i < 2; ++i)
		state_.delta[i] = state_.current[i] - previous[i];
}
}
