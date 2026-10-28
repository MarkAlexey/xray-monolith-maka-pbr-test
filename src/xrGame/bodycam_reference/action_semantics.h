#pragma once
#include "animation_blend.h"
#include "action_lifecycle.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace BodycamReference
{
// The action semantics package: per action clip, the random placement offset
// and the curves the clip publishes while the engine plays it.
class ActionSemantics
{
public:
	ActionSemantics(const std::vector<std::uint8_t>& bytes, std::map<std::uint64_t, unsigned> bindings, std::function<double()> random);
	bool SameKind(std::uint64_t a, std::uint64_t b) const;
	bool HasMotion(std::uint64_t motion) const { return bindings_.count(motion) != 0; }
	void SetReloadPhase(ReloadPhase phase) { lifecycle_.SetReloadPhase(phase); }
	double ReloadSamplePosition() const { return samples_.empty() ? 0 : samples_.front().seconds / samples_.front().duration; }
	float ReloadSampleWeight() const;
	bool ReloadSequenceActive() const { return lifecycle_.ReloadSequenceActive(); }
	bool Admit(std::uint64_t motion, std::uint64_t generation, bool crouched, bool empty, bool aimed = false,
		bool suppressSharedLookDown = false, std::optional<unsigned> sourceClip = {});
	void Samples(std::vector<ActionSample> samples) { samples_ = lifecycle_.Playback(std::move(samples)); }
	bool PreserveEnginePlacement() const { return preserveEnginePlacement_; }
	double EnginePlacementWeight() const;
	bool SuppressesSharedLookDown(std::uint64_t motion) const;
	static bool SharedLookDownCurve(const std::string& name);
	// The source montage fades the shared look-down curves over half a second at each end of the
	// clip, on the clip's own time. Only those curves use this envelope.
	static float SharedLookDownWeight(double sourceSeconds, double sourceDuration, float engineWeight);
	const Vector& Offset() const { return offset_; }
	AnimationCurves Apply(const AnimationCurves& base) const;

private:
	struct Key
	{
		double time, value;
		bool linear;
	};
	struct Clip
	{
		bool reload;
		double duration;
		Vector standingMin, standingMax, crouchedMin, crouchedMax;
		std::map<std::string, std::vector<Key>> curves;
	};
	std::vector<Clip> clips_;
	std::map<std::uint64_t, unsigned> bindings_;
	struct AdmittedMotion
	{
		unsigned clip;
		bool preserveEnginePlacement;
		bool suppressLookDown;
	};
	std::map<std::uint64_t, AdmittedMotion> admitted_;
	std::function<double()> random_;
	ActionLifecycle lifecycle_;
	std::vector<ActionSample> samples_;
	Vector offset_{};
	bool preserveEnginePlacement_ = false;
};
}
