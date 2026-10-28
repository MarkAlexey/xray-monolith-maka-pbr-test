// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "inertial.h"
#include <algorithm>
namespace BodycamReference
{
float InertialDecay(float x, float v, float t, float T)
{
	t = std::max(t, 0.f);
	float sign = 1;
	if (t >= T - 1e-7f)
		return 0;
	if (x < 0)
	{
		x = -x;
		v = -v;
		sign = -1;
	}
	v = v < 0 ? v : 0;
	if (v < -1e-4f)
		T = std::min((x * -5.f) / v, T);
	if (t >= T - 1e-7f)
		return 0;
	const float T2 = T * T, T3 = T2 * T, T4 = T3 * T, T5 = T4 * T, twentyX = x * 20.f;
	const float acceleration = std::max(((T * -8.f) * v - twentyX) / T2, 0.f), aT2 = (acceleration * 1.5f) * T2;
	const float A = (x * -6.f - ((acceleration * T2) * .5f + (T * 3.f) * v)) / T5;
	const float B = (((T * 8.f) * v + aT2) + x * 15.f) / T4;
	const float C = (twentyX * -.5f - ((T * 6.f) * v + aT2)) / T3;
	float value = A * t + B;
	value = value * t + C;
	value = value * t + acceleration * .5f;
	value = value * t + v;
	value = value * t + x;
	return value * sign;
}
void InertialTiming::Initialize()
{
	queue_.clear();
	accumulatedDelta = 0;
	Deactivate();
}
void InertialTiming::Deactivate()
{
	state = 0;
	elapsed = duration = maximumDuration = deficit = 0;
}
void InertialTiming::Update(double dt)
{
	accumulatedDelta = float(dt) + accumulatedDelta;
}
bool InertialTiming::Request(InertialRequest request)
{
	if (request.duration < 0)
		return false;
	for (const auto& r : queue_)
		if (r.duration == request.duration && r.flag == request.flag && r.blendOption == request.blendOption)
			return false;
	queue_.push_back(request);
	return true;
}
InertialEvent InertialTiming::BeginEvaluation(bool recent)
{
	const unsigned count = unsigned(queue_.size());
	bool activated = false;
	float subtraction = 0;
	if (count && recent)
	{
		if (state == 2)
		{
			const float remaining = duration - elapsed;
			if (deficit > 0)
				subtraction = remaining;
			deficit = remaining;
		}
		state = 1;
		elapsed = 0;
		activated = true;
		duration = std::max(queue_[0].duration - subtraction, 0.f);
		for (unsigned i = 1; i < count; ++i)
			duration = std::min(duration, std::max(queue_[i].duration - subtraction, 0.f));
		maximumDuration = duration;
	}
	queue_.clear();
	if (state != 0)
	{
		elapsed = accumulatedDelta + elapsed;
		deficit = elapsed >= duration ? 0 : deficit - std::min(accumulatedDelta, deficit);
		if (elapsed >= maximumDuration)
			Deactivate();
	}
	return { count, activated, subtraction, state, elapsed, duration, accumulatedDelta, state == 1, state != 0 };
}
void InertialTiming::OffsetsInitialized()
{
	state = 2;
}
std::vector<InertialCurveDelta> BuildInertialCurves(
	const std::vector<InertialCurve>& current, const std::vector<InertialCurve>& recent, const std::vector<InertialCurve>& older, float dt)
{
	std::vector<InertialCurveDelta> out;
	for (const auto& e : current)
		out.push_back({ e.name, e.value, 0, 0, e.flags });
	unsigned i = 0, j = 0;
	while (j < recent.size())
	{
		const auto& source = recent[j];
		if (i == out.size())
		{
			out.push_back({ source.name, 0, source.value, 0, 0 });
			++i;
			++j;
			continue;
		}
		auto& r = out[i];
		if (r.name == source.name)
		{
			r.displacement = source.value - r.current;
			++i;
			++j;
		}
		else if (r.name < source.name)
		{
			r.displacement = -r.current;
			++i;
		}
		else
		{
			out.insert(out.begin() + i, { source.name, 0, source.value, 0, 0 });
			++i;
			++j;
		}
	}
	if (dt > 1e-4f)
	{
		i = j = 0;
		while (j < older.size())
		{
			const auto& source = older[j];
			if (i == out.size())
			{
				const float reciprocal = 1.f / dt;
				for (; j < older.size(); ++j)
					out.push_back({ older[j].name, 0, 0, (0.f - older[j].value) * reciprocal, 0 });
				break;
			}
			auto& r = out[i];
			if (r.name == source.name)
			{
				r.velocity = ((r.displacement - r.current) - source.value) / dt;
				++i;
				++j;
			}
			else if (r.name < source.name)
			{
				r.velocity = (r.displacement - r.current) / dt;
				++i;
			}
			else
			{
				out.insert(out.begin() + i, { source.name, 0, 0, (0.f - source.value) / dt, 0 });
				++i;
				++j;
			}
		}
	}
	return out;
}
std::vector<InertialCurve> ApplyInertialCurves(
	const std::vector<InertialCurve>& current, const std::vector<InertialCurveDelta>& delta, float time, float duration)
{
	std::vector<InertialCurve> out;
	unsigned i = 0, j = 0;
	while (i < current.size() || j < delta.size())
	{
		const int order = i == current.size() ? 1
			: j == delta.size()				  ? -1
			: current[i].name < delta[j].name ? -1
			: current[i].name > delta[j].name ? 1
											  : 0;
		const auto* source = order <= 0 ? &current[i] : nullptr;
		const auto* d = order >= 0 ? &delta[j] : nullptr;
		const float offset = InertialDecay(d ? d->displacement : 0, d ? d->velocity : 0, time, duration);
		out.push_back({ source ? source->name : d->name, source ? offset + source->value : offset,
			std::uint8_t((source ? source->flags : 0) | (d ? d->flags : 0)) });
		if (order <= 0)
			++i;
		if (order >= 0)
			++j;
	}
	return out;
}
void InertialCurves::Initialize()
{
	timing.Initialize();
	recent_ = {};
	older_ = {};
	deltas_.clear();
}
void InertialCurves::Deactivate()
{
	timing.Deactivate();
	deltas_.clear();
}
InertialCurveResult InertialCurves::Evaluate(const std::vector<InertialCurve>& current, unsigned boneCount)
{
	const auto event = timing.BeginEvaluation(recent_.boneCount != 0);
	if (event.state == 0)
		deltas_.clear();
	if (event.initializeOffsets)
	{
		const auto& older = older_.boneCount ? older_ : recent_;
		deltas_ = BuildInertialCurves(current, recent_.curves, older.curves, recent_.dt);
		timing.OffsetsInitialized();
	}
	auto output = event.applyOffsets ? ApplyInertialCurves(current, deltas_, event.elapsed, event.duration) : current;
	if (recent_.boneCount)
		std::swap(recent_, older_);
	recent_ = { boneCount, event.accumulatedDelta, output };
	timing.FinishEvaluation();
	return { std::move(output), event };
}
}
