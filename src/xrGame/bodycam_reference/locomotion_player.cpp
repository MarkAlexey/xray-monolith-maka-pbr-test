// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "locomotion_player.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
LocomotionPlayer::LocomotionPlayer(std::string identity, float startPosition, LocomotionGrid grid, std::array<float, 2> windows,
	std::map<std::string, MarkerSequence> sequences, LocomotionPlayerState initial)
	: assetIdentity_(std::move(identity)), startPosition_(startPosition), grid_(std::move(grid)),
	  filters_{ BlendSpaceFir(windows[0], FirType::Cubic), BlendSpaceFir(windows[1], FirType::Cubic) }, sequences_(std::move(sequences)),
	  state_(std::move(initial))
{
	if (assetIdentity_.empty() || !(startPosition_ > 0 && startPosition_ <= 1))
		throw std::invalid_argument("Locomotion player needs an asset identity and a start position in (0, 1]");
	for (unsigned i = 0; i < 4; ++i)
	{
		const auto& sequence = sequences_.at(grid_.Sequences()[i].identity);
		durations_[i] = DurationSequence{ sequence.length, sequence.rateScale };
	}
}
void LocomotionPlayer::Initialize(const std::function<void(LocomotionPlayerState&)>& updateExposedInputs)
{
	auto next = state_;
	next.node.previous.index = kNoMarker;
	next.node.next.index = kNoMarker;
	next.relevanceFlag = false;
	updateExposedInputs(next);
	next.samples.clear();
	next.node.normalizedTime = startPosition_;
	next.node.previous.index = kNoMarker;
	next.node.next.index = kNoMarker;
	for (auto& filter : filters_)
		filter.Reset();
	state_ = std::move(next);
}
NamedCurves LocomotionPlayer::Evaluate(const std::map<std::string, CompressedLocomotionCurves>& samplers,
	const std::map<std::string, std::set<std::string>>& admittedNames) const
{
	std::vector<WeightedNamedCurves> evaluated;
	for (const auto& cached : state_.samples)
	{
		const auto& s = cached.marker;
		const float length = sequences_.at(s.sequenceIdentity).length;
		const float time = std::max(0.f, std::min(s.time, length)), weight = std::clamp(float(s.weight), 0.f, 1.f);
		evaluated.push_back({ weight, samplers.at(s.sequenceIdentity).Sample(time, admittedNames.at(s.sequenceIdentity)) });
	}
	return BlendWeightedCurves(evaluated);
}
LocomotionPlayerTick LocomotionPlayer::Tick(std::array<double, 2> inputs, double recordRate, CandidateContext context)
{
	state_.historyDelta = context.deltaTime * float(recordRate);
	state_.previousTime = state_.node.normalizedTime;
	state_.markerFlag = true;
	const std::array<float, 2> filtered = { filters_[0].Update(float(inputs[0]), context.deltaTime),
		filters_[1].Update(float(inputs[1]), context.deltaTime) };
	const auto gridSamples = grid_.Samples(filtered[0], filtered[1]);
	auto samples = RebuildLocomotionSamples(state_.samples, gridSamples, grid_.Sequences(), true);
	const float duration = BlendSpaceDuration(gridSamples, durations_);
	std::vector<BlendMarkerSample> markerSamples;
	for (const auto& s : samples)
		markerSamples.push_back(s.marker);
	auto tick = TickLocomotionMarkers(std::move(markerSamples), sequences_, state_.node, std::move(context), state_.historyDelta);
	for (unsigned i = 0; i < samples.size(); ++i)
		samples[i].marker = tick.samples[i];
	state_.node = tick.node;
	state_.samples = std::move(samples);
	return { std::move(tick), filtered, duration };
}
}
