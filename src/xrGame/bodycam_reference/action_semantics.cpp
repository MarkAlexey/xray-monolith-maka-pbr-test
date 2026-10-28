#include "action_semantics.h"
#include "asset_reader.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
ActionSemantics::ActionSemantics(
	const std::vector<std::uint8_t>& bytes, std::map<std::uint64_t, unsigned> bindings, std::function<double()> random)
	: bindings_(std::move(bindings)), random_(std::move(random))
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRACTS1", 1);
	const auto count = r.Count(32);
	clips_.reserve(count);
	for (unsigned n = 0; n < count; ++n)
	{
		Clip c;
		c.reload = r.Bool();
		c.duration = r.F64();
		if (c.duration <= 0)
			throw std::invalid_argument("Action clip duration is not positive");
		for (auto* v : { &c.standingMin, &c.standingMax, &c.crouchedMin, &c.crouchedMax })
			for (auto& x : *v)
				x = r.F64();
		const auto curves = r.Count(512);
		for (unsigned j = 0; j < curves; ++j)
		{
			const auto name = r.String();
			const auto keys = r.Count(4096);
			std::vector<Key> values;
			values.reserve(keys);
			for (unsigned k = 0; k < keys; ++k)
			{
				Key key{ r.F64(), r.F64(), r.Bool() };
				if (!values.empty() && key.time < values.back().time)
					throw std::invalid_argument("Action curve keys are out of time order");
				values.push_back(key);
			}
			if (!c.curves.emplace(name, std::move(values)).second)
				throw std::invalid_argument("Action clip has two curves with one name");
		}
		clips_.push_back(std::move(c));
	}
	if (!r.Done() || !random_ || bindings_.empty())
		throw std::invalid_argument("Action semantics have trailing data, no random source or no bindings");
	for (const auto& binding : bindings_)
		if (binding.second >= clips_.size())
			throw std::invalid_argument("Action binding names a clip past the end of the package");
}
bool ActionSemantics::SameKind(std::uint64_t a, std::uint64_t b) const
{
	const auto x = bindings_.find(a), y = bindings_.find(b);
	return x != bindings_.end() && y != bindings_.end() && x->second == y->second;
}
float ActionSemantics::ReloadSampleWeight() const
{
	float total = 0;
	for (const auto& sample : samples_)
		total += sample.weight;
	return std::min(total, 1.f);
}
bool ActionSemantics::Admit(std::uint64_t motion, std::uint64_t generation, bool crouched, bool empty, bool aimed,
	bool suppressSharedLookDown, std::optional<unsigned> sourceClip)
{
	const auto found = bindings_.find(motion);
	if (found == bindings_.end())
		return false;
	if (lifecycle_.AlreadyAdmitted(motion, generation))
		return false;
	unsigned selected = sourceClip.value_or(found->second);
	if (selected >= clips_.size())
		throw std::invalid_argument("Action clip index past the end of the package");
	if (!sourceClip && empty && !clips_[selected].reload && selected + 1 < clips_.size() && !clips_[selected + 1].reload)
		++selected;
	const auto admission = lifecycle_.Admit(motion, generation, clips_[selected].reload, selected, suppressSharedLookDown);
	if (admission.continuation)
	{
		admitted_[motion] = { admission.clip, true, admission.suppressLookDown };
		return true;
	}
	const auto& clip = clips_[selected];
	// Reload and aimed clips already carry their placement; the random root offset is not added.
	preserveEnginePlacement_ = clip.reload || aimed;
	admitted_[motion] = { selected, preserveEnginePlacement_, suppressSharedLookDown };
	const auto& min = crouched ? clip.crouchedMin : clip.standingMin;
	const auto& max = crouched ? clip.crouchedMax : clip.standingMax;
	for (unsigned i = 0; i < 3; ++i)
	{
		const auto random = random_();
		if (!std::isfinite(random) || random < 0 || random >= 1)
			throw std::invalid_argument("Action random value is outside [0, 1)");
		offset_[i] = min[i] + (max[i] - min[i]) * random;
	}
	return true;
}
double ActionSemantics::EnginePlacementWeight() const
{
	double total = 0, preserved = 0;
	for (const auto& sample : samples_)
	{
		if (!HasMotion(sample.motion) || sample.weight <= 0)
			continue;
		total += sample.weight;
		const auto policy = admitted_.find(sample.motion);
		if (policy != admitted_.end() && policy->second.preserveEnginePlacement)
			preserved += sample.weight;
	}
	// Without samples the last policy holds through the action-weight fade-out.
	return total > 0 ? preserved / total : (preserveEnginePlacement_ ? 1. : 0.);
}
bool ActionSemantics::SuppressesSharedLookDown(std::uint64_t motion) const
{
	const auto it = admitted_.find(motion);
	return it != admitted_.end() && it->second.suppressLookDown;
}
bool ActionSemantics::SharedLookDownCurve(const std::string& name)
{
	return name == "Enable_SpineRotation" || name == "Camera_Pitch" || name == "Camera_Yaw" || name == "Camera_Roll";
}
float ActionSemantics::SharedLookDownWeight(double sourceSeconds, double sourceDuration, float engineWeight)
{
	constexpr double blendSeconds = .5;
	const double envelope = std::clamp(std::min(sourceSeconds, sourceDuration - sourceSeconds) / blendSeconds, 0., 1.);
	// Cap, rather than multiply, so an engine crossfade is not eased twice.
	return std::min(engineWeight, static_cast<float>(envelope));
}
AnimationCurves ActionSemantics::Apply(const AnimationCurves& base) const
{
	AnimationCurves weighted;
	double total = 0;
	for (const auto& sample : samples_)
	{
		const auto found = bindings_.find(sample.motion);
		if (found == bindings_.end() || sample.weight == 0)
			continue;
		const auto admitted = admitted_.find(sample.motion);
		const auto& clip = clips_[admitted == admitted_.end() ? found->second : admitted->second.clip];
		const bool suppressLookDown = admitted != admitted_.end() && admitted->second.suppressLookDown;
		// Engine playback sets the phase; the clip is sampled at the same fraction of its duration.
		const double time = std::clamp(sample.seconds / sample.duration, 0., 1.) * clip.duration;
		total += sample.weight;
		for (const auto& curve : clip.curves)
		{
			if (suppressLookDown && SharedLookDownCurve(curve.first))
			{
				// Keep the incoming curve, or its absence; each sample keeps the policy it was admitted with.
				const auto incoming = base.find(curve.first);
				if (incoming != base.end())
					weighted[curve.first] += incoming->second * sample.weight;
				continue;
			}
			const auto& keys = curve.second;
			if (keys.empty())
				continue;
			unsigned i = 0;
			while (i + 1 < keys.size() && keys[i + 1].time <= time)
				++i;
			const auto& a = keys[i];
			const auto& b = keys[std::min(i + 1, unsigned(keys.size() - 1))];
			const double alpha = &a == &b ? 0 : std::clamp((time - a.time) / (b.time - a.time), 0., 1.);
			const float value = float(a.value + (a.linear ? (b.value - a.value) * alpha : 0));
			if (SharedLookDownCurve(curve.first))
			{
				const float lookDownWeight = SharedLookDownWeight(time, clip.duration, sample.weight);
				const auto incoming = base.find(curve.first);
				// Preserve an absent base curve at a fully faded boundary.
				if (lookDownWeight == 0 && incoming == base.end())
					continue;
				const float baseValue = incoming == base.end() ? 0.f : incoming->second;
				weighted[curve.first] += baseValue * (sample.weight - lookDownWeight) + value * lookDownWeight;
			}
			else
				weighted[curve.first] += value * sample.weight;
		}
	}
	if (total == 0)
		return base;
	for (auto& value : weighted)
		value.second = float(value.second / total);
	return BlendAnimationCurves(base, weighted, float(std::min(total, 1.)));
}
}
