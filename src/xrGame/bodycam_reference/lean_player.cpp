#include "lean_player.h"
#include "constants.h"
#include "selection_sort.h"
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
LeanPlayer::LeanPlayer(std::array<CookedGridPoint, 25> grid, LeanPlayerState initial) : grid_(std::move(grid)), state_(std::move(initial))
{
}
void LeanPlayer::Initialize(const std::function<void()>& base, const std::function<void()>& exposed)
{
	if (base)
		base();
	state_.node.previous.index = kNoMarker;
	state_.node.next.index = kNoMarker;
	state_.relevanceFlag = false;
	if (exposed)
		exposed();
	state_.samples.clear();
	state_.node.normalizedTime = 0;
	state_.node.previous.index = kNoMarker;
	state_.node.next.index = kNoMarker;
}
LeanRegistration LeanPlayer::Register(
	double weight, double root, double lr, double fb, const std::function<void(const LeanRegistration&)>& sink)
{
	state_.blendWeight = float(weight);
	state_.relevanceFlag = state_.relevanceFlag || float(weight) >= kBlendFull;
	state_.inputs = { float(lr), float(fb) };
	LeanRegistration record{ float(weight), float(root), state_.inputs };
	sink(record);
	return record;
}
std::vector<LeanSample> LeanPlayer::Samples(double x, double y) const
{
	x = (std::clamp(x, -1.0, 1.0) + 1) / .5f;
	y = (std::clamp(y, -1.0, 1.0) + 1) / .5f;
	const int ix = int(x), iy = int(y);
	const double fx = x - ix, fy = y - iy;
	const std::array<float, 4> weights = { float((1 - fy) * (1 - fx)), float((1 - fy) * fx), float((1 - fx) * fy), float(fy * fx) };
	const std::array<int, 4> corners = { iy * 5 + ix, iy * 5 + ix + 1, (iy + 1) * 5 + ix, (iy + 1) * 5 + ix + 1 };
	// At ix == 4 the ix + 1 corners fall into the next row with weight 0; they still add zero-weight samples to the sort.
	std::vector<LeanSample> samples;
	for (unsigned c = 0; c < 4; ++c)
	{
		const int cell = corners[c];
		if (cell >= 25)
			continue;
		const auto& grid = grid_[cell];
		for (unsigned lane = 0; lane < 3; ++lane)
		{
			const int index = grid.indices[lane];
			if (index < 0 || index >= 5)
				continue;
			auto found = std::find_if(samples.begin(), samples.end(), [&](const auto& s) { return s.sampleIndex == unsigned(index); });
			if (found == samples.end())
			{
				samples.push_back({ unsigned(index), 0 });
				found = samples.end() - 1;
			}
			found->weight = weights[c] * grid.weights[lane] + found->weight;
		}
	}
	SortByDescendingWeight(samples, [](const LeanSample& s) { return s.weight; });
	float total = 0;
	unsigned count = 0;
	while (count < samples.size() && samples[count].weight >= kBlendEpsilon)
	{
		total += samples[count].weight;
		++count;
	}
	samples.resize(count);
	if (count)
	{
		const float reciprocal = 1.f / total;
		for (auto& s : samples)
			s.weight = reciprocal * s.weight;
	}
	return samples;
}
LeanTick LeanPlayer::Tick(double dt, const LeanRegistration& record)
{
	const float previous = state_.node.normalizedTime, move = float(dt);
	state_.historyDelta = move;
	state_.previousTime = previous;
	state_.markerFlag = true;
	state_.filteredInputs = record.inputs;
	auto samples = Samples(record.inputs[0], record.inputs[1]);
	if (samples.empty())
		return { false, 0, previous, previous };
	state_.samples = std::move(samples);
	constexpr float length = .033333335f;
	float duration = 0;
	for (const auto& sample : state_.samples)
		duration += length * std::clamp(sample.weight, 0.f, 1.f);
	float time = duration * previous + move;
	if (time < 0 || time > duration)
	{
		time = std::fmod(time, duration);
		if (time < 0)
			time += duration;
	}
	const float current = time / duration;
	state_.node.normalizedTime = current;
	const float oldRatio = std::clamp(previous, 0.f, 1.f), ratio = std::clamp(current, 0.f, 1.f);
	for (auto& sample : state_.samples)
		if (sample.weight > kBlendEpsilon)
		{
			const float old = length * oldRatio;
			sample.time = length * ratio;
			float delta = sample.time - old;
			if (move * delta < 0)
				delta += length * (move > 0 ? 1.f : -1.f);
			sample.historyDelta = delta;
			sample.historyPreviousTime = old;
			sample.historyFlag = true;
		}
	return { true, duration, previous, current };
}
void LeanAlpha::Initialize(const std::function<void()>& base, const std::function<void()>& lean)
{
	base();
	lean();
	initialized = false;
}
float LeanAlpha::Update(double dt, const LocomotionStateUpdate& context, bool lod, bool valid,
	const std::function<void(const LocomotionStateUpdate&)>& base, const std::function<void()>& exposed,
	const std::function<double()>& previousWeightGait, const std::function<void(const LocomotionStateUpdate&)>& lean)
{
	base(context);
	actual = 0;
	if (!lod)
		return actual;
	exposed();
	if (!valid)
		return actual;
	const float target = std::clamp(float(previousWeightGait()) - 1.f, .5f, 1.f);
	float result = target;
	if (initialized)
	{
		const float speed = target >= cached ? 20.f : 1.f, distance = target - cached;
		if (!(distance * distance < 1e-8f))
			result = std::clamp(speed * float(dt), 0.f, 1.f) * distance + cached;
	}
	cached = result;
	initialized = true;
	actual = result;
	if (actual > kBlendEpsilon)
	{
		auto child = context;
		child.weight = actual * context.weight;
		lean(child);
	}
	return actual;
}
float FootRootAlpha::Update(const LocomotionStateUpdate& context, bool lod, const std::function<void(const LocomotionStateUpdate&)>& child,
	const std::function<double()>& diagonal)
{
	child(context);
	actual = 0;
	if (!lod)
		return actual;
	alpha = float(diagonal());
	// + 0.f turns -0 into +0.
	actual = std::clamp(alpha + 0.f, 0.f, 1.f);
	return actual;
}
}
