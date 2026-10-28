// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_secondary.h"
#include <cmath>

namespace BodycamReference
{
void TranslationRecoil::Configure(const TranslationRecoilConfig& config)
{
	config_ = config;
	for (unsigned i = 0; i < 4; ++i)
		if (config.acceleration[i] != 0)
			state_.acceleration[i] = config.acceleration[i];
}

void TranslationRecoil::Fire(const std::array<double, 2>& units, bool aiming)
{
	for (unsigned i = 0; i < 2; ++i)
	{
		const double amplitude = config_.noiseAmplitude[i];
		state_.target[i] += units[i] * (amplitude - (-amplitude)) + (-amplitude);
	}
	state_.target[3] += double(static_cast<float>(config_.progressAmount)) * (aiming ? config_.progressAimAlpha : 1.);
}

void TranslationRecoil::SetAutoPushback()
{
	state_.target[2] = static_cast<float>(config_.pushback);
}

void TranslationRecoil::Update(double dt, bool aiming)
{
	for (unsigned i = 0; i < 3; ++i)
	{
		state_.target[i] *= std::exp(-dt * config_.damping[i]);
		state_.value[i] += (state_.target[i] - state_.value[i]) * (1 - std::exp(-dt * state_.acceleration[i]));
		if (i != 2 && aiming)
			state_.value[i] *= config_.noiseAimScalar;
	}
	state_.value[3] += (state_.target[3] - state_.value[3]) * (1 - std::exp(-dt * state_.acceleration[3]));
	state_.target[3] *= std::exp(-dt * config_.damping[3]);
}

void SwayRecoil::Fire(const std::array<double, 2>& units, bool aiming)
{
	const double scale = aiming ? config_.adsScale : 1.;
	for (unsigned i = 0; i < 2; ++i)
	{
		const auto& range = config_.range[i];
		const float sample = static_cast<float>(units[i] * (range[1] - range[0]) + range[0]);
		state_.target[i] += static_cast<float>(double(sample) * scale);
	}
}

void SwayRecoil::Update(double dt)
{
	const double follow = 1 - std::exp(-config_.acceleration * dt);
	const double decay = std::exp(-config_.damping * dt);
	for (unsigned i = 0; i < 2; ++i)
	{
		state_.value[i] += (state_.target[i] - state_.value[i]) * follow;
		state_.target[i] *= decay;
	}
	state_.value[2] = state_.value[1] * config_.rollMultiplier;
}
}
