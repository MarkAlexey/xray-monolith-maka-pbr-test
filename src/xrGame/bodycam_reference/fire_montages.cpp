#include "fire_montages.h"
#include "constants.h"
#include <algorithm>
namespace BodycamReference
{
namespace
{
// The montage lasts .9 s and starts blending out .5 s before its end.
constexpr float kFireMontageSeconds = .9f, kBlendOutSeconds = .5f;
}
void FireMontages::Stop(FireMontageInstance& instance, double duration)
{
	const auto& state = instance.blend.State();
	if (state.end != 0 || float(duration) < state.duration)
		instance.blend.Reset(0, duration);
}
void FireMontages::StopAll(double duration)
{
	for (auto& instance : instances_)
		Stop(instance, duration);
}
unsigned FireMontages::Play()
{
	StopAll(kFireBlendSeconds);
	const unsigned id = nextId_++;
	instances_.push_back({ id });
	return id;
}
const std::vector<FireMontageSample>& FireMontages::Update(double delta)
{
	const float dt = float(delta);
	for (auto& instance : instances_)
		instance.blend.Update(dt);
	for (auto& instance : instances_)
	{
		const float movement = std::min(dt, kFireMontageSeconds - instance.time);
		instance.time = instance.time + movement;
		const float remaining = kFireMontageSeconds - instance.time;
		if (instance.blend.State().end != 0 && remaining <= kBlendOutSeconds)
			Stop(instance, remaining);
	}
	instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
						 [](const auto& i) { return i.blend.State().end == 0 && i.blend.State().value == 0; }),
		instances_.end());
	std::vector<float> raw;
	float total = 0;
	for (const auto& instance : instances_)
		if (instance.blend.State().value > kBlendEpsilon)
		{
			raw.push_back(instance.blend.State().value);
			total = total + raw.back();
		}
	const auto weights = AdditiveSlotWeights(raw, total);
	samples_.clear();
	for (const auto& instance : instances_)
		if (instance.blend.State().value > kBlendEpsilon)
			samples_.push_back(
				{ instance.id, instance.time, weights[samples_.size()], instance.blend.State().value, instance.blend.State().end == 0 });
	return samples_;
}
}
