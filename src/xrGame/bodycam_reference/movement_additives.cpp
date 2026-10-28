#include "movement_additives.h"
#include "constants.h"
#include "upper_pose.h"
#include "viewmodel_pose.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
const std::array<const char*, 6> names = { "IK_Pitch", "IK_Yaw", "IK_Roll", "IK_X", "IK_Y", "IK_Z" };
constexpr float kWalkCycleSeconds = 3.0666666f;
NamedCurves EvaluateTwoWay(const TwoWayPoseState& s, const std::function<NamedCurves()>& a, const std::function<NamedCurves()>& b)
{
	if (!s.relevant[1])
		return a();
	if (!s.relevant[0])
		return b();
	const auto left = a(), right = b();
	return TwoWayCurves(left, right, s.alpha);
}
}
MovementWalkCurves::MovementWalkCurves(std::vector<WalkingCurve> curves) : curves_(std::move(curves))
{
	std::set<std::string> remaining(names.begin(), names.end());
	for (const auto& c : curves_)
	{
		if (!remaining.erase(c.name) || c.keys.size() != 93)
			throw std::invalid_argument("Walking curves must be the six IK curves with 93 keys each");
		for (unsigned i = 0; i < c.keys.size(); ++i)
			if (!std::isfinite(c.keys[i].time) || !std::isfinite(c.keys[i].value) || (i && c.keys[i].time <= c.keys[i - 1].time))
				throw std::invalid_argument("Walking curve keys must be finite and increase in time");
	}
	if (!remaining.empty())
		throw std::invalid_argument("Walking curve set is missing an IK curve");
}
NamedCurves MovementWalkCurves::Sample(float time) const
{
	NamedCurves out;
	for (const auto& c : curves_)
	{
		float value;
		if (time <= c.keys.front().time)
			value = c.keys.front().value;
		else if (time >= c.keys.back().time)
			value = c.keys.back().value;
		else
		{
			unsigned lo = 0, hi = unsigned(c.keys.size() - 1);
			while (hi - lo > 1)
			{
				const unsigned mid = (lo + hi) >> 1;
				if (time < c.keys[mid].time)
					hi = mid;
				else
					lo = mid;
			}
			const auto& a = c.keys[lo];
			const auto& b = c.keys[hi];
			const float alpha = (time - a.time) / (b.time - a.time);
			value = a.value + alpha * (b.value - a.value);
		}
		out.push_back({ c.name, value, 0 });
	}
	return out;
}
MovementAdditivesWalk::MovementAdditivesWalk(MovementWalkCurves curves, WalkingStressAdapter stress)
	: curves_(std::move(curves)), stress_(std::move(stress))
{
	for (unsigned id : { 25, 26, 45, 44, 50 })
		state_.alphas.emplace(id, ProceduralAlphaState{});
}
void MovementAdditivesWalk::Visit(unsigned id, float weight, float dt, bool aiming, float gait, bool& machineUpdated)
{
	if (id == 50)
		Visit(44, weight, dt, aiming, gait, machineUpdated);
	else if (id == 44)
		Visit(45, weight, dt, aiming, gait, machineUpdated);
	else if (id == 45)
		Visit(26, weight, dt, aiming, gait, machineUpdated);
	auto& s = state_.alphas.at(id);
	s.actual = 0;
	s.raw = id == 26 && aiming ? .3f : 1.f;
	const float target = s.raw;
	float result = target;
	if (id != 25)
	{
		if (s.initialized)
		{
			const float distance = target - s.cached;
			if (!(distance * distance < 1e-8f))
				result = std::clamp(10.f * dt, 0.f, 1.f) * distance + s.cached;
		}
		s.cached = result;
	}
	s.initialized = true;
	s.actual = std::clamp(result, 0.f, 1.f);
	if (s.actual <= kBlendEpsilon)
		return;
	if (id == 25)
		Visit(50, weight * s.actual, dt, aiming, gait, machineUpdated);
	else if (id == 26)
	{
		machineUpdated = true;
		if (!state_.machineRelevant)
			state_.time = 0;
		const std::array<float, 2> alphas = { std::clamp(gait - 1.f, 0.f, 1.f), std::clamp(gait, 0.f, 1.f) };
		for (unsigned i = 0; i < 2; ++i)
			snapshots_[i] = { alphas[i], { alphas[i]<kBlendFull, alphas[i]> kBlendEpsilon } };
		state_.playerUpdated = snapshots_[0].relevant[0] && snapshots_[1].relevant[1];
		if (state_.playerUpdated)
		{
			const float advance = dt * .6f * locomotionRate_;
			float next = state_.time + advance;
			if (next > kWalkCycleSeconds || next < 0)
			{
				next = std::fmod(next, kWalkCycleSeconds);
				if (next < 0)
					next += kWalkCycleSeconds;
			}
			state_.time = next;
		}
	}
	else if (stress_.tick && (id == 44 || id == 50))
		stress_.tick(id == 44 ? 28 : 51, dt);
}
void MovementAdditivesWalk::SetLocomotionSpeed(double speedCm)
{
	if (!std::isfinite(speedCm) || speedCm < 0)
		throw std::invalid_argument("Locomotion speed must be finite and not negative");
	locomotionRate_ = float(speedCm / 150.);
	locomotionTarget_ = std::clamp(locomotionRate_, 0.f, 1.f);
	if (!hasLocomotionSpeed_)
		locomotionWeight_ = locomotionTarget_;
	hasLocomotionSpeed_ = true;
}
void MovementAdditivesWalk::Update(double delta, bool aiming, const AnimationCurves& curves)
{
	const float dt = float(delta);
	// The measured speed sets the walking cadence; the walking detail amplitude follows it with a
	// frame-rate independent exponential response. Stress and action curves are not scaled.
	if (hasLocomotionSpeed_)
		locomotionWeight_ += (locomotionTarget_ - locomotionWeight_) * (-std::expm1(-12.f * dt));
	state_.inFlight = true;
	state_.consumed = state_.published;
	state_.layerWeight = 1.f - CurveValue(curves, "Mask_IK_Movement");
	state_.layerRelevant = state_.layerWeight > kBlendEpsilon;
	state_.playerUpdated = false;
	bool updated = false;
	if (state_.layerRelevant)
		Visit(25, state_.layerWeight, dt, aiming, CurveValue(curves, "Weight_Gait"), updated);
	state_.machineRelevant = updated;
}
NamedCurves MovementAdditivesWalk::EvaluateNode(unsigned id, const NamedCurves& input) const
{
	NamedCurves base;
	if (id == 25)
		base = input;
	else if (id == 50)
		base = EvaluateNode(44, input);
	else if (id == 44)
		base = EvaluateNode(45, input);
	else if (id == 45)
		base = EvaluateNode(26, input);
	const float alpha = state_.alphas.at(id).actual;
	NamedCurves delta;
	if (alpha > kBlendEpsilon)
	{
		if (id == 25)
			delta = EvaluateNode(50, input);
		else if (id == 26)
			delta = EvaluateTwoWay(
				snapshots_[0],
				[&] { return EvaluateTwoWay(snapshots_[1], [] { return NamedCurves{}; }, [&] { return curves_.Sample(state_.time); }); },
				[] { return NamedCurves{}; });
		else if (id == 44 || id == 50)
		{
			const unsigned scaleId = id == 44 ? 29 : 49, sequence = id == 44 ? 28 : 51;
			const auto scales = stress_.scales ? stress_.scales(scaleId) : std::array<float, 6>{};
			delta = stress_.sample ? stress_.sample(sequence) : NamedCurves{};
			for (unsigned i = 0; i < 6; ++i)
				delta = ScaleNamedCurve(std::move(delta), names[i], scales[i]);
		}
	}
	return AdditiveCurves(base, delta, id == 26 ? alpha * locomotionWeight_ : alpha);
}
NamedCurves MovementAdditivesWalk::Evaluate(const NamedCurves& input)
{
	auto output = input;
	if (state_.layerRelevant)
	{
		for (const auto& e : EvaluateNode(25, input))
		{
			const auto it = std::find_if(output.begin(), output.end(), [&](const auto& old) { return old.name == e.name; });
			if (it == output.end())
				output.push_back(e);
			else
				*it = e;
		}
	}
	state_.published = output;
	state_.inFlight = false;
	++state_.epoch;
	return output;
}
Matrix ApplyMovementAdditivesWalk(const Matrix& matrix, const NamedCurves& curves)
{
	AnimationCurves values;
	for (const auto& e : curves)
		values[e.name] = e.value;
	return ApplyMovementAdditives(matrix, values);
}
}
