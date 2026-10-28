#pragma once
#include "recoil_curve.h"
#include <array>
#include <cstdint>

namespace BodycamReference
{
// Rotation channels roll/pitch/yaw, then location X/Y/Z.
using RecoilChannels = std::array<double, 6>;
using RecoilCurves = std::array<RecoilCurve, 6>;
struct RecoilTimelineState
{
	RecoilChannels value{}, base{}, targets{};
	std::array<bool, 6> returning{};
	float position = 0, length = 0;
	double playRate = 1;
	std::uint64_t eventCount = 0, playCount = 0;
	bool active = false;
};
class RecoilTimeline
{
public:
	explicit RecoilTimeline(RecoilCurves curves);
	void SetCurves(RecoilCurves curves);
	void SetTargets(const RecoilChannels& targets) { state_.targets = targets; }
	void SetPlayRate(double rate) { state_.playRate = rate; }
	void Play();
	// Returns true when the shot needs new random targets; set them after this call.
	bool Tick(double delta, double worldDelta);
	const RecoilTimelineState& State() const { return state_; }

private:
	void Evaluate(float time, float previous);
	RecoilCurves curves_;
	RecoilTimelineState state_{};
};
}
