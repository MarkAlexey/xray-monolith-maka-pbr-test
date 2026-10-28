#pragma once
#include <array>
#include <cstdint>

namespace BodycamReference
{
struct FireSwayConfig
{
	std::array<float, 2> step{ 1.f, 1.f }, limit{};
	double damping = 0, interpolation = 0;
};
struct FireSwayState
{
	std::array<double, 2> target{}, current{}, value{};
	std::uint32_t shotIndex = 0;
	float gate = 0;
	bool holdTargets = false;
};
class FireSway
{
public:
	void Configure(const FireSwayConfig& config) { config_ = config; }
	void Stop()
	{
		state_.shotIndex = 0;
		state_.holdTargets = false;
	}
	void HoldTargets(bool hold) { state_.holdTargets = hold; }
	// The sampler receives the shot index as a signed float.
	template <class Sampler>
	void Fire(Sampler&& sample)
	{
		ApplyShot(sample(AdvanceShot()));
	}
	void Update(double dt, double enableAds);
	const FireSwayState& State() const { return state_; }

private:
	float AdvanceShot();
	void ApplyShot(double sampledAmplitude);
	FireSwayConfig config_{};
	FireSwayState state_{};
};
}
