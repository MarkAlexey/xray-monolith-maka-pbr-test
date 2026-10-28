#pragma once
#include "montage_blend.h"
#include "recoil_random.h"
#include <optional>

namespace BodycamReference
{
// Regrip clips blend in and out over this many seconds.
constexpr double kRegripBlendSeconds = .2;
struct RegripEventState
{
	std::optional<float> forcedDelay, endFireDelay, cooldown;
	bool idleRegripBlocked = false, aiming = false;
};
class RegripEvents
{
public:
	RegripEvents(RecoilRandom& random, unsigned clipCount, std::function<double(unsigned)> play);
	void AimChanged(bool aiming, bool cancelled = false);
	// Sets the aim state without requesting a regrip.
	void SeedAim(bool aiming) { state_.aiming = aiming; }
	// Plays a regrip after delay seconds, at once for zero.
	void Request(double delay = kRegripBlendSeconds);
	void RequestEndFire();
	// Plays a regrip unless reloading or the cooldown of the last one is running.
	bool TryIdleRegrip(bool reloading);
	// Plays the given clip, or a random one.
	void PlayRegrip(std::optional<unsigned> clip = {});
	void Update(double deltaTime, bool reloading = false);
	const RegripEventState& State() const { return state_; }

private:
	RecoilRandom& random_;
	unsigned clipCount_;
	std::function<double(unsigned)> play_;
	RegripEventState state_;
};
class SemiEndFireRegrip
{
public:
	SemiEndFireRegrip(RecoilRandom& random, std::function<void()> request);
	void Begin(double worldTime, bool local = true);
	bool End(bool cancelled = false, bool local = true, bool recoilValid = true, bool instabilityValid = true);
	double FireStartTime() const { return fireStartTime_; }
	double LastFireTime() const { return lastFireTime_; }
	std::optional<float> LastProbability() const { return lastProbability_; }

private:
	// Seconds between automatic shots, and the scale of the end-of-burst regrip probability.
	static constexpr float kFireInterval = float(.09), kInstability = 1.5f;
	RecoilRandom& random_;
	std::function<void()> request_;
	double fireStartTime_ = 0, lastFireTime_ = 0;
	std::optional<float> lastProbability_;
};
struct RegripMontageInstance
{
	unsigned id, clip;
	float time = 0;
	MontageBlend blend{ 0, 1, kRegripBlendSeconds, MontageBlendShape::Sinusoidal };
};
struct RegripMontageSample
{
	unsigned id, clip;
	float time, weight, rawWeight;
	bool retiring;
};
class RegripMontages
{
public:
	RegripMontages(std::vector<double> durations, RecoilRandom& random);
	RegripMontages(const RegripMontages&) = delete;
	RegripMontages& operator=(const RegripMontages&) = delete;
	unsigned Play(unsigned clip);
	// Blends every playing clip out over the given seconds.
	void StopAll(double duration);
	// Takes over the clips other is playing (a fresh scheduler keeps a fade going).
	void TakeClips(RegripMontages& other);
	const std::vector<RegripMontageSample>& Update(double deltaTime, bool tickEvents = true, bool reloading = false);
	RegripEvents& Events() { return events_; }
	SemiEndFireRegrip& Semi() { return semi_; }
	const std::vector<RegripMontageInstance>& Instances() const { return instances_; }

private:
	static void Stop(RegripMontageInstance&, double duration);
	unsigned Start(unsigned clip);
	std::vector<double> durations_;
	std::vector<RegripMontageInstance> instances_;
	std::vector<RegripMontageSample> samples_;
	unsigned nextId_ = 1, lastPlayedId_ = 0;
	RegripEvents events_;
	SemiEndFireRegrip semi_;
};
}
