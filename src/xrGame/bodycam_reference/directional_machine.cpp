// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "directional_machine.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
namespace BodycamReference
{
ChangeDirectionCurve::ChangeDirectionCurve(std::array<DirectionCurveKey, 3> keys) : keys_(keys)
{
	for (const auto& key : keys)
		if (!std::isfinite(key.value) || !std::isfinite(key.arrive) || !std::isfinite(key.leave))
			throw std::invalid_argument("Change-direction curve key is not finite");
}
float ChangeDirectionCurve::Sample(float t) const
{
	if (t < 0)
		return keys_[0].value;
	if (t > 1)
		return keys_[2].value;
	const unsigned i = t < .5f ? 0 : 1;
	const auto& left = keys_[i];
	const auto& right = keys_[i + 1];
	const float u = (t - (i == 0 ? 0.f : .5f)) / .5f, third = 1.f / 3.f;
	const float a = left.value, b = (.5f * left.leave) * third + a, c = right.value - (.5f * right.arrive) * third, d = right.value;
	const float d0 = b - a, d1 = c - b, d2 = d - c, l0 = d0 * u + a, l1 = d1 * u + b, l2 = d2 * u + c;
	const float u0 = (l1 - l0) + d0, u1 = (l2 - l1) + d1, v0 = u0 * u + a, v1 = u1 * u + b;
	return ((v1 - v0) + u0) * u + a;
}
float ChangeDirectionCurve::Alpha(float input) const
{
	return std::clamp(Sample(input), 0.f, 1.f);
}
namespace
{
struct Edge
{
	unsigned previous, rule, next;
	float duration;
};
constexpr Edge edges[] = { { 0, 82, 2, .7f }, { 0, 83, 4, .7f }, { 0, 84, 1, .5f }, { 1, 82, 3, .7f }, { 1, 83, 5, .7f }, { 1, 85, 0, .5f },
	{ 2, 83, 5, .5f }, { 2, 84, 3, .7f }, { 2, 85, 0, .7f }, { 2, 86, 3, .75f }, { 3, 83, 4, .5f }, { 3, 85, 2, .7f }, { 3, 87, 2, .75f },
	{ 3, 84, 1, .7f }, { 3, 88, 2, .75f }, { 4, 82, 3, .5f }, { 4, 84, 5, .7f }, { 4, 85, 0, .7f }, { 4, 88, 5, .75f }, { 5, 82, 2, .5f },
	{ 5, 85, 4, .7f }, { 5, 89, 4, .75f }, { 5, 84, 1, .7f }, { 5, 86, 4, .75f } };
}
DirectionalMachine::DirectionalMachine(ChangeDirectionCurve curve, unsigned state, std::vector<float> weights, bool first)
	: LocomotionMachine(6, state, std::move(weights), first, 3), curve_(curve)
{
}
std::optional<LocomotionTransitionChoice> DirectionalMachine::SelectTransition(bool)
{
	const bool center = std::abs(hip_) < .5f;
	const bool rules[] = { direction_ == 1, direction_ == 2, direction_ == 3, direction_ == 0, hip_ < -.5f && feet_ == 0,
		center && PublicWeight(3) == 1 && feet_ == 0, hip_ > .5f && feet_ == 0, center && PublicWeight(5) == 1 && feet_ == 0 };
	for (unsigned id = 0; id < std::size(edges); ++id)
	{
		const auto& e = edges[id];
		if (e.previous == currentState_ && rules[e.rule - 82])
		{
			selected_.push_back(id);
			return LocomotionTransitionChoice{ e.next, e.duration };
		}
	}
	return std::nullopt;
}
float DirectionalMachine::Duration(const LocomotionTransitionChoice& c, float weight) const
{
	return float(c.duration) * std::clamp(1.f - weight, 0.f, 1.f);
}
LocomotionBlend DirectionalMachine::NewBlend(const LocomotionTransitionChoice& c, float duration) const
{
	// The .75 s transitions follow the change-direction curve.
	CubicRangeBlend blend(c.duration == .75 ? std::function<float(float)>([curve = curve_](float x) { return curve.Alpha(x); })
											: std::function<float(float)>{});
	blend.SetRange(0, 1);
	blend.SetDuration(duration);
	return blend;
}
float DirectionalMachine::StackAlpha(const LocomotionTransitionRecord& r) const
{
	return std::get<CubicRangeBlend>(r.blend).MapAlpha(r.duration > 0 ? r.elapsed / r.duration : 0);
}
DirectionalTrace DirectionalMachine::Update(double dt, unsigned direction, double hip, double feet, double parent, bool active)
{
	direction_ = direction;
	hip_ = float(hip);
	feet_ = float(feet);
	selected_.clear();
	const bool suppress = firstUpdate_;
	DirectionalTrace trace;
	trace.machine = LocomotionMachine::Update(dt, false, parent, active);
	trace.machine.cyclesUpdated = false;
	trace.selectedEdges = selected_;
	if (!suppress)
		for (unsigned id : selected_)
			if (id == 2 || id == 5 || id == 6 || id == 10 || id == 15 || id == 19)
				trace.queuedNotifies.push_back({ 14, id, edges[id].previous, edges[id].next });
	return trace;
}
}
