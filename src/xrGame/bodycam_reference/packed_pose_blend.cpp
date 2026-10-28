// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "packed_pose_blend.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
PackedTransform Scale(PackedTransform t, float weight)
{
	for (auto& value : t)
		value *= weight;
	return t;
}
PackedTransform Accumulate(PackedTransform destination, const PackedTransform& source, float weight)
{
	const auto v = Scale(source, weight);
	const double xz = v[0] * destination[0] + v[2] * destination[2], yw = v[1] * destination[1] + v[3] * destination[3];
	const bool positive = 0 <= yw + xz;
	for (unsigned i = 0; i < 12; ++i)
		destination[i] = (i < 4 && !positive ? 0 - v[i] : v[i]) + destination[i];
	return destination;
}
PackedPose Combine(const std::vector<WeightedPackedPose>& samples, bool finalNormalize)
{
	PackedPose out = samples.front().pose;
	for (auto& t : out)
		t = Scale(t, samples.front().weight);
	for (unsigned i = 1; i < samples.size(); ++i)
	{
		const auto& sample = samples[i];
		if (sample.pose.size() != out.size())
			throw std::invalid_argument("Blended poses differ in bone count");
		for (unsigned bone = 0; bone < out.size(); ++bone)
			out[bone] = Accumulate(out[bone], sample.pose[bone], sample.weight);
	}
	if (samples.size() > 1)
		out = NormalizePackedPose(std::move(out));
	if (finalNormalize)
		out = NormalizePackedPose(std::move(out));
	return out;
}
template <std::size_t N>
std::vector<unsigned> Relevant(const std::array<float, N>& weights)
{
	std::vector<unsigned> out;
	for (unsigned i = 0; i < N; ++i)
		if (weights[i] > 1e-5f)
			out.push_back(i);
	return out;
}
}
PackedPose PackPose(const std::vector<SourceBoneTransform>& pose)
{
	PackedPose out;
	for (const auto& t : pose)
		out.push_back({ t.q[0], t.q[1], t.q[2], t.q[3], t.p[0], t.p[1], t.p[2], 0, t.s[0], t.s[1], t.s[2], 0 });
	return out;
}
PackedTransform NormalizePackedTransform(PackedTransform t)
{
	const double xz = t[0] * t[0] + t[2] * t[2], yw = t[1] * t[1] + t[3] * t[3], s = yw + xz;
	if (s < 1e-8)
	{
		t[0] = t[1] = t[2] = 0;
		t[3] = 1;
	}
	else
	{
		const double reciprocal = 1 / std::sqrt(s);
		for (unsigned i = 0; i < 4; ++i)
			t[i] *= reciprocal;
	}
	return t;
}
PackedPose NormalizePackedPose(PackedPose pose)
{
	for (auto& t : pose)
		t = NormalizePackedTransform(t);
	return pose;
}
PackedPose BlendSpacePackedPose(const std::vector<WeightedPackedPose>& samples)
{
	if (samples.empty() || samples.size() > 4)
		throw std::invalid_argument("Blend space needs one to four poses");
	return Combine(samples, true);
}
PackedPose LeanPackedPose(const std::vector<WeightedPackedPose>& samples)
{
	if (samples.empty() || samples.size() > 5)
		throw std::invalid_argument("Lean blend needs one to five poses");
	return Combine(samples, true);
}
PackedPose TwoWayPackedPose(float alpha, bool aRelevant, bool bRelevant, const std::function<PackedPose(unsigned)>& child)
{
	if (!bRelevant)
		return child(0);
	if (!aRelevant)
		return child(1);
	const float aw = 1.f - alpha, bw = 1.f - aw;
	auto a = child(0), b = child(1);
	return Combine({ { aw, std::move(a) }, { bw, std::move(b) } }, false);
}
PackedPose GaitPackedPose(
	std::array<float, 2> weights, const std::function<PackedPose(unsigned)>& child, const std::function<PackedPose()>& reference)
{
	const auto rows = Relevant(weights);
	if (rows.empty())
		return reference();
	if (rows.size() == 1 && weights[rows[0]] >= .99999f)
		return child(rows[0]);
	std::vector<WeightedPackedPose> samples;
	for (unsigned i : rows)
		samples.push_back({ weights[i], child(i) });
	return Combine(samples, false);
}
PackedPose MultiWayPackedPose(
	std::array<float, 4> weights, const std::function<PackedPose(unsigned)>& child, const std::function<PackedPose()>& reference)
{
	const auto rows = Relevant(weights);
	if (rows.empty())
		return reference();
	std::vector<WeightedPackedPose> samples;
	for (unsigned i : rows)
		samples.push_back({ weights[i], child(i) });
	return Combine(samples, true);
}
TransitionWeights WeightFactorPair(double alpha, double scale)
{
	if (!std::isfinite(scale))
		throw std::invalid_argument("Transition profile scale is not finite");
	const float a = float(alpha), s = float(scale);
	float incoming = std::max(s * a, 1e-5f), outgoing = s > 1e-5f ? std::max((1.f - a) / s, 1e-5f) : 1.f, sum = incoming + outgoing;
	if (sum > 1e-5f)
	{
		if (std::abs(sum - 1.f) > 1e-5f)
		{
			incoming /= sum;
			outgoing /= sum;
		}
	}
	else
		incoming = outgoing = .5f;
	return { outgoing, incoming };
}
PackedTransform TransitionPackedTransform(const PackedTransform& a, const PackedTransform& b, TransitionWeights w)
{
	return Accumulate(Scale(a, w.outgoing), b, w.incoming);
}
PackedPose TransitionPackedPose(
	const PackedPose& a, const PackedPose& b, double alpha, const std::vector<int>& indices, const std::vector<TransitionWeights>& weights)
{
	if (a.size() != b.size() || a.size() != indices.size())
		throw std::invalid_argument("Transition poses and profile indices differ in size");
	const float incoming = float(alpha);
	const TransitionWeights scalar{ 1.f - incoming, incoming };
	PackedPose out;
	out.reserve(a.size());
	for (unsigned i = 0; i < a.size(); ++i)
	{
		const int index = indices[i];
		if (index < -1 || index >= int(weights.size()))
			throw std::invalid_argument("Transition profile index out of range");
		const auto w = index == -1 ? scalar : weights[index];
		out.push_back(TransitionPackedTransform(a[i], b[i], w));
	}
	return out;
}
}
