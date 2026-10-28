#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
namespace BodycamReference
{
struct ActionSample
{
	std::uint64_t motion;
	double seconds, duration;
	float weight;
};
enum class ReloadPhase
{
	None,
	Open,
	Insert,
	Close
};

// Tracks a reload sequence across the engine's open, insert and close clips and maps their
// playback onto one action position. Engine playback is the only clock.
class ActionLifecycle
{
	std::optional<std::pair<std::uint64_t, std::uint64_t>> admitted_;
	ReloadPhase phase_ = ReloadPhase::None;
	std::map<std::uint64_t, ReloadPhase> motionPhases_;
	bool sequence_ = false, sampled_ = false, suppressLookDown_ = false;
	std::uint64_t motion_ = 0;
	unsigned clip_ = 0;
	double position_ = 0;
	float weight_ = 0;

public:
	struct Admission
	{
		bool continuation;
		unsigned clip;
		bool suppressLookDown;
	};
	bool AlreadyAdmitted(std::uint64_t motion, std::uint64_t generation) const
	{
		return admitted_ && *admitted_ == std::make_pair(motion, generation);
	}
	void SetReloadPhase(ReloadPhase phase)
	{
		phase_ = phase;
		if (phase == ReloadPhase::None)
		{
			sequence_ = false;
			sampled_ = false;
		}
	}
	bool ReloadSequenceActive() const { return sequence_; }
	void Reset() { *this = ActionLifecycle{}; }
	Admission Admit(std::uint64_t motion, std::uint64_t generation, bool reload, unsigned clip, bool suppressLookDown)
	{
		admitted_ = std::make_pair(motion, generation);
		if (phase_ != ReloadPhase::None && reload)
		{
			motionPhases_[motion] = phase_;
			if (sequence_)
				return { true, clip_, suppressLookDown_ };
			sequence_ = true;
			sampled_ = false;
			motion_ = motion;
			clip_ = clip;
			position_ = 0;
			weight_ = 0;
			suppressLookDown_ = suppressLookDown;
		}
		else
			motionPhases_.erase(motion);
		return { false, clip, suppressLookDown };
	}
	static double ReloadPosition(ReloadPhase phase, double enginePosition)
	{
		const auto p = std::clamp(enginePosition, 0., 1.);
		return phase == ReloadPhase::Open ? .5 * p : phase == ReloadPhase::Insert ? .5 : phase == ReloadPhase::Close ? .5 + .5 * p : p;
	}
	std::vector<ActionSample> Playback(std::vector<ActionSample> samples)
	{
		float weight = 0;
		const ActionSample* current = nullptr;
		for (auto& sample : samples)
		{
			if (!std::isfinite(sample.seconds) || !std::isfinite(sample.duration) || !std::isfinite(sample.weight) || sample.seconds < 0 ||
				sample.duration <= 0 || sample.weight < 0 || sample.weight > 1)
				throw std::invalid_argument("Action sample time, duration or weight is out of range");
			const auto phase = motionPhases_.find(sample.motion);
			if (phase == motionPhases_.end())
				continue;
			sample.seconds = ReloadPosition(phase->second, sample.seconds / sample.duration) * sample.duration;
			if (sequence_ && phase->second == phase_)
			{
				weight += sample.weight;
				if (!current || sample.weight > current->weight)
					current = &sample;
			}
		}
		if (!sequence_)
			return samples;
		if (current)
		{
			position_ = current->seconds / current->duration;
			sampled_ = true;
		}
		if (phase_ == ReloadPhase::Insert)
			position_ = .5;
		if (phase_ == ReloadPhase::Open)
		{
			if (current)
				weight_ = std::min(weight, 1.f);
		}
		else if (sampled_)
			weight_ = 1;
		// One sample stands for the sequence, so clip swaps and zero-weight first samples do not end it.
		return { { motion_, position_, 1, weight_ } };
	}
};
}
