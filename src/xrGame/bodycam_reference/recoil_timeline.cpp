// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_timeline.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace BodycamReference
{
RecoilTimeline::RecoilTimeline(RecoilCurves curves) : curves_(std::move(curves)) {}
void RecoilTimeline::SetCurves(RecoilCurves curves)
{
	curves_ = std::move(curves);
}
void RecoilTimeline::Play()
{
	state_.base = state_.value;
	state_.returning = {};
	// Only rotation is a timeline track. Location must not extend its duration.
	state_.length = std::max({ 0.f, curves_[0].LastTime(), curves_[1].LastTime(), curves_[2].LastTime() });
	state_.position = 0;
	Evaluate(0, 0);
	state_.active = true;
	++state_.playCount;
}
bool RecoilTimeline::Tick(double dt, double worldDelta)
{
	if (!state_.active)
		return false;
	const float old = state_.position;
	const float next = float(double(old) + float(double(float(state_.playRate)) * float(dt)));
	state_.position = std::min(next, state_.length);
	const float previous = std::max(float(double(state_.position) - float(double(float(state_.playRate)) * float(worldDelta))), 0.f);
	Evaluate(state_.position, previous);
	state_.active = next <= state_.length;
	const bool event = old <= 0 && state_.position > 0;
	if (event)
		++state_.eventCount;
	return event;
}
void RecoilTimeline::Evaluate(float time, float previous)
{
	for (unsigned i = 0; i < 6; ++i)
	{
		const float value = curves_[i].Sample(time);
		const float old = curves_[i].Sample(previous);
		if (!state_.returning[i] && std::abs(old) > std::abs(value))
		{
			state_.base[i] = 0;
			state_.returning[i] = true;
		}
		const double base = state_.base[i];
		state_.value[i] = base + (state_.targets[i] - base) * value;
	}
}
}
