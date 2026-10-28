// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "regrip_montages.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
// An end of fire requests a regrip after a random delay in this range, in seconds.
constexpr double kEndFireDelayMin = .1, kEndFireDelayMax = .5;
// After a regrip the idle regrip stays blocked for a random time between the clip duration and this.
constexpr double kCooldownMax = 4;
// The end-of-burst regrip probability at the shortest gap between bursts, before the instability scale.
constexpr double kBaseProbability = .02;
}
RegripEvents::RegripEvents(RecoilRandom& random, unsigned count, std::function<double(unsigned)> play)
	: random_(random), clipCount_(count), play_(std::move(play))
{
}
void RegripEvents::AimChanged(bool value, bool cancelled)
{
	if (value == state_.aiming)
		return;
	state_.aiming = value;
	if (value || !cancelled)
		Request();
}
void RegripEvents::Request(double delay)
{
	const float d = float(delay);
	if (d == 0)
		PlayRegrip();
	else
		state_.forcedDelay = d;
}
void RegripEvents::RequestEndFire()
{
	state_.endFireDelay = float(random_.Range(kEndFireDelayMin, kEndFireDelayMax));
}
bool RegripEvents::TryIdleRegrip(bool reloading)
{
	if (reloading || state_.idleRegripBlocked)
		return false;
	state_.idleRegripBlocked = true;
	PlayRegrip();
	return true;
}
void RegripEvents::PlayRegrip(std::optional<unsigned> clip)
{
	const unsigned index =
		clip ? *clip : std::min(unsigned(std::trunc(double(float(double(random_.Unit()) * float(clipCount_))))), clipCount_ - 1);
	const float duration = float(play_(index));
	const float next = float(random_.Range(duration, kCooldownMax));
	if (!state_.cooldown)
		state_.cooldown = next;
}
void RegripEvents::Update(double deltaTime, bool reloading)
{
	const float dt = float(deltaTime);
	const std::array<std::optional<float>*, 3> actions{ &state_.cooldown, &state_.forcedDelay, &state_.endFireDelay };
	std::array<bool, 3> due{};
	for (unsigned i = 0; i < actions.size(); ++i)
		if (*actions[i])
		{
			**actions[i] -= dt;
			if (**actions[i] <= 0)
			{
				actions[i]->reset();
				due[i] = true;
			}
		}
	// Snapshot/decrement first: newly scheduled delays wait for the next tick.
	if (due[0])
		state_.idleRegripBlocked = false;
	if (due[1])
		PlayRegrip();
	if (due[2])
		TryIdleRegrip(reloading);
}
SemiEndFireRegrip::SemiEndFireRegrip(RecoilRandom& random, std::function<void()> request) : random_(random), request_(std::move(request)) {}
void SemiEndFireRegrip::Begin(double worldTime, bool local)
{
	if (local)
		fireStartTime_ = worldTime;
}
bool SemiEndFireRegrip::End(bool cancelled, bool local, bool recoilValid, bool instabilityValid)
{
	if (cancelled || !local || !recoilValid)
		return false;
	bool requested = false;
	if (instabilityValid)
	{
		const double lo = double(kFireInterval) + .1, hi = double(kFireInterval) * 2;
		const double alpha = std::clamp((fireStartTime_ - lastFireTime_ - lo) / (hi - lo), 0., 1.);
		lastProbability_ = float((kBaseProbability + (1 - kBaseProbability) * alpha) * kInstability);
		if (*lastProbability_ > 0 && *lastProbability_ >= random_.Unit())
		{
			request_();
			requested = true;
		}
	}
	lastFireTime_ = fireStartTime_;
	fireStartTime_ = 0;
	return requested;
}
RegripMontages::RegripMontages(std::vector<double> durations, RecoilRandom& random)
	: durations_(std::move(durations)), events_(random, static_cast<unsigned>(durations_.size()),
											[this](unsigned clip)
											{
												lastPlayedId_ = Start(clip);
												return durations_[clip];
											}),
	  semi_(random, [this] { events_.RequestEndFire(); })
{
	if (durations_.empty())
		throw std::invalid_argument("Regrip needs at least one clip");
	for (double duration : durations_)
		if (!std::isfinite(float(duration)) || duration <= 0)
			throw std::invalid_argument("Regrip clip duration must be positive");
}
void RegripMontages::Stop(RegripMontageInstance& instance, double duration)
{
	const auto& blend = instance.blend.State();
	if (blend.end != 0 || float(duration) < blend.duration)
		instance.blend.Reset(0, duration);
}
unsigned RegripMontages::Start(unsigned clip)
{
	for (auto& instance : instances_)
		Stop(instance, kRegripBlendSeconds);
	const unsigned id = nextId_++;
	instances_.push_back({ id, clip });
	return id;
}
void RegripMontages::StopAll(double duration)
{
	for (auto& instance : instances_)
		Stop(instance, duration);
}
void RegripMontages::TakeClips(RegripMontages& other)
{
	instances_ = std::move(other.instances_);
	nextId_ = other.nextId_;
	lastPlayedId_ = other.lastPlayedId_;
}
unsigned RegripMontages::Play(unsigned clip)
{
	events_.PlayRegrip(clip);
	return lastPlayedId_;
}
const std::vector<RegripMontageSample>& RegripMontages::Update(double deltaTime, bool tickEvents, bool reloading)
{
	const float dt = float(deltaTime);
	if (tickEvents)
		events_.Update(dt, reloading);
	for (auto& instance : instances_)
		instance.blend.Update(dt);
	for (auto& instance : instances_)
	{
		const float end = float(durations_[instance.clip]);
		const float movement = std::min(dt, end - instance.time);
		instance.time += movement;
		if (instance.time >= end)
			Stop(instance, 0);
	}
	instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
						 [](const auto& i) { return i.blend.State().end == 0 && i.blend.State().value == 0; }),
		instances_.end());
	samples_.clear();
	float total = 0;
	for (const auto& instance : instances_)
		if (instance.blend.State().value > float(1e-5))
		{
			const auto& blend = instance.blend.State();
			total += blend.value;
			samples_.push_back({ instance.id, instance.clip, instance.time, 0, blend.value, blend.end == 0 });
		}
	const float scale = AdditiveSlotScale(total);
	for (auto& sample : samples_)
		sample.weight = sample.rawWeight * scale;
	return samples_;
}
}
