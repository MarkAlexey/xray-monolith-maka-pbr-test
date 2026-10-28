// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "additive_pose.h"
#include "constants.h"
#include "detail_players.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
PackedTransform ApplyLocalAdditive(const PackedTransform& base, const PackedTransform& delta, float alpha)
{
	if (alpha <= kBlendEpsilon)
		return base;
	const double a = alpha;
	const bool full = alpha >= kBlendFull;
	std::array<double, 4> q;
	if (full)
		for (unsigned i = 0; i < 4; ++i)
			q[i] = delta[i] * a;
	else
	{
		for (unsigned i = 0; i < 4; ++i)
		{
			const double identity = i == 3 ? 1 : 0;
			q[i] = (delta[i] >= 0 ? identity : 0 - identity) * (1 - a) + a * delta[i];
		}
		const double sum = (q[3] * q[3] + q[1] * q[1]) + (q[2] * q[2] + q[0] * q[0]);
		if (sum >= 1e-8)
		{
			const double reciprocal = 1 / std::sqrt(sum);
			for (auto& v : q)
				v = reciprocal * v;
		}
		else
			q = { 0, 0, 0, 1 };
	}
	const double x = q[0], y = q[1], z = q[2], w = q[3], bx = base[0], by = base[1], bz = base[2], bw = base[3];
	PackedTransform out;
	if (full)
	{
		out[0] = (z * by) * -1 + (y * bz + (x * bw + w * bx));
		out[1] = z * bx + (y * bw + ((x * bz) * -1 + w * by));
		out[2] = z * bw + ((x * by + w * bz) - y * bx);
		out[3] = (z * bz) * -1 + (((x * bx) * -1 + w * bw) - y * by);
	}
	else
	{
		out[0] = ((x * bw + w * bx) + y * bz) + (z * by) * -1;
		out[1] = (((x * bz) * -1 + w * by) + y * bw) + z * bx;
		out[2] = ((x * by + w * bz) + (y * bx) * -1) + z * bw;
		out[3] = (((x * bx) * -1 + w * bw) + (y * by) * -1) + (z * bz) * -1;
	}
	const double zero = full ? 0 : (1 - a) * 0;
	out[4] = a * delta[4] + base[4];
	out[5] = a * delta[5] + base[5];
	out[6] = (zero + a * delta[6]) + base[6];
	out[7] = (zero + a * delta[7]) + base[7];
	out[8] = (a * delta[8] + 1) * base[8];
	out[9] = (a * delta[9] + 1) * base[9];
	out[10] = ((zero + a * delta[10]) + 1) * base[10];
	out[11] = ((zero + a * delta[11]) + 0) * base[11];
	return out;
}
NamedCurves AdditiveCurves(const NamedCurves& base, const NamedCurves& delta, float alpha)
{
	auto out = base;
	if (alpha > kBlendEpsilon)
		for (const auto& e : delta)
		{
			auto& found = FindOrAddCurve(out, e.name);
			found.value = alpha * e.value + found.value;
			found.flags |= e.flags;
		}
	return out;
}
PoseCurveBundle EvaluateLocalAdditive(
	float alpha, const std::function<PoseCurveBundle()>& base, const std::function<PoseCurveBundle()>& additive)
{
	auto out = base();
	if (alpha <= kBlendEpsilon)
		return out;
	const auto delta = additive();
	if (out.pose.size() != delta.pose.size())
		throw std::invalid_argument("Additive pose and base pose differ in size");
	for (unsigned i = 0; i < out.pose.size(); ++i)
		out.pose[i] = ApplyLocalAdditive(out.pose[i], delta.pose[i], alpha);
	out.pose = NormalizePackedPose(std::move(out.pose));
	out.pose = NormalizePackedPose(std::move(out.pose));
	out.curves = AdditiveCurves(out.curves, delta.curves, alpha);
	return out;
}
PoseCurveBundle EvaluateDetailState(unsigned state, float alpha, std::array<float, 4> weights,
	const std::function<PoseCurveBundle()>& cycles, const std::function<PoseCurveBundle(unsigned)>& player,
	const std::function<PoseCurveBundle()>& reference)
{
	if (state == 0 || state == 2)
		return cycles();
	const auto& order = DetailPlayerOrder(state);
	return EvaluateLocalAdditive(alpha, cycles,
		[&]
		{
			std::array<std::optional<PoseCurveBundle>, 4> memo;
			auto pose = MultiWayPackedPose(
				weights,
				[&](unsigned i)
				{
					memo[i] = player(order[i]);
					return memo[i]->pose;
				},
				[&] { return reference().pose; });
			std::vector<WeightedNamedCurves> rows;
			for (unsigned i = 0; i < 4; ++i)
				rows.push_back({ weights[i], memo[i] ? memo[i]->curves : NamedCurves{} });
			return PoseCurveBundle{ std::move(pose), MultiWayCurves(rows) };
		});
}
}
