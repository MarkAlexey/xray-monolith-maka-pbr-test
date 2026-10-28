// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_fire_sway.h"
#include <cmath>

namespace BodycamReference
{
float FireSway::AdvanceShot()
{
	++state_.shotIndex;
	return static_cast<float>(std::int32_t(state_.shotIndex));
}
void FireSway::ApplyShot(double sampledAmplitude)
{
	const float amplitude = static_cast<float>(sampledAmplitude);
	for (unsigned i = 0; i < 2; ++i)
	{
		const double sum = state_.target[i] + static_cast<float>(double(config_.step[i]) * amplitude);
		const double upper = sum < config_.limit[i] ? sum : config_.limit[i];
		state_.target[i] = upper > -config_.limit[i] ? upper : -config_.limit[i];
	}
}
void FireSway::Update(double dt, double enableAds)
{
	state_.gate = static_cast<float>(1. - static_cast<float>(enableAds));
	const double decay = std::exp(-config_.damping * dt);
	const double follow = 1 - std::exp(-config_.interpolation * dt);
	for (unsigned i = 0; i < 2; ++i)
	{
		if (!state_.holdTargets)
			state_.target[i] *= decay;
		state_.current[i] += (state_.target[i] - state_.current[i]) * follow;
		state_.value[i] = state_.current[i] * state_.gate;
	}
}
}
