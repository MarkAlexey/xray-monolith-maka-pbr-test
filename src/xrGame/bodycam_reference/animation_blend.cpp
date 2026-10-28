// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "animation_blend.h"
#include "constants.h"
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
std::array<float, 2> Weights(double alpha)
{
	const float a = float(1. - float(alpha)), b = float(1. - a);
	if (!std::isfinite(a) || a < 0 || a > 1)
		throw std::invalid_argument("Animation alpha must be in [0,1]");
	return { a, b };
}
}
LocalPose BlendAnimationLocal(const LocalPose& from, const LocalPose& to, double alpha)
{
	const auto weights = Weights(alpha);
	Quaternion a, b;
	for (unsigned i = 0; i < 4; ++i)
	{
		a[i] = from.rotation[i] * weights[0];
		b[i] = to.rotation[i] * weights[1];
	}
	const double dot = (a[0] * b[0] + a[2] * b[2]) + (a[1] * b[1] + a[3] * b[3]);
	const double sign = dot >= 0 ? 1 : -1;
	LocalPose result;
	for (unsigned i = 0; i < 4; ++i)
		result.rotation[i] = a[i] + sign * b[i];
	auto& q = result.rotation;
	const double square = (q[0] * q[0] + q[2] * q[2]) + (q[1] * q[1] + q[3] * q[3]);
	if (square >= 1e-8)
	{
		const double inverse = 1 / std::sqrt(square);
		for (auto& v : q)
			v *= inverse;
	}
	else
		q = { 0, 0, 0, 1 };
	for (unsigned i = 0; i < 3; ++i)
	{
		result.position[i] = from.position[i] * weights[0] + to.position[i] * weights[1];
		result.scale[i] = from.scale[i] * weights[0] + to.scale[i] * weights[1];
	}
	return result;
}
AnimationCurves BlendAnimationCurves(const AnimationCurves& from, const AnimationCurves& to, double alpha)
{
	const float b = Weights(alpha)[1];
	if (std::abs(b) <= kBlendEpsilon)
		return from;
	if (std::abs(float(double(b) - 1)) <= kBlendEpsilon)
		return to;
	AnimationCurves result;
	for (const auto& entry : from)
	{
		const auto next = to.find(entry.first);
		result[entry.first] = next == to.end()
			? float(double(entry.second) - float(double(entry.second) * b))
			: float(double(entry.second) + float(double(float(double(next->second) - entry.second)) * b));
	}
	for (const auto& entry : to)
		if (from.find(entry.first) == from.end())
			result[entry.first] = float(double(entry.second) * b);
	return result;
}
void AccumulateMeshSpacePose(const std::vector<LocalPose>& base, const std::vector<int>& parents, const std::vector<AdditivePose>& deltas,
	double weight, MeshSpaceScratch& scratch, std::vector<LocalPose>& result)
{
	const float w = float(weight);
	if (base.size() != parents.size() || base.size() != deltas.size())
		throw std::invalid_argument("Additive pose counts differ");
	if (!std::isfinite(w) || w < 0 || w > 1)
		throw std::invalid_argument("Additive weight must be in [0,1]");
	const Quaternion identity{ 0, 0, 0, 1 };
	auto& current = scratch.current;
	auto& components = scratch.components;
	current.resize(base.size());
	components.resize(base.size());
	result.resize(base.size());
	for (unsigned i = 0; i < base.size(); ++i)
	{
		if (parents[i] < -1 || parents[i] >= static_cast<int>(i))
			throw std::invalid_argument("Additive parents must precede children");
		current[i] = NormalizeQuaternion(QuaternionProduct(parents[i] < 0 ? identity : current[parents[i]], base[i].rotation));
	}
	const bool active = w > kBlendEpsilon;
	for (unsigned i = 0; i < base.size(); ++i)
	{
		const auto& d = deltas[i];
		const auto& q = d.rotation;
		const double sign = q[3] < 0 ? -1 : 1;
		const auto weighted =
			active ? NormalizeQuaternion({ q[0] * sign * w, q[1] * sign * w, q[2] * sign * w, 1 + (q[3] * sign - 1) * w }) : identity;
		components[i] = NormalizeQuaternion(QuaternionProduct(weighted, current[i]));
		for (unsigned j = 0; j < 3; ++j)
		{
			result[i].position[j] = base[i].position[j] + (active ? d.position[j] * w : 0);
			result[i].scale[j] = base[i].scale[j] * (1 + (active ? d.scaleDelta[j] * w : 0));
		}
	}
	for (unsigned i = 0; i < base.size(); ++i)
		result[i].rotation =
			NormalizeQuaternion(QuaternionProduct(Conjugate(parents[i] < 0 ? identity : components[parents[i]]), components[i]));
}
std::vector<LocalPose> AccumulateMeshSpacePose(
	const std::vector<LocalPose>& base, const std::vector<int>& parents, const std::vector<AdditivePose>& deltas, double weight)
{
	MeshSpaceScratch scratch;
	std::vector<LocalPose> result;
	AccumulateMeshSpacePose(base, parents, deltas, weight, scratch, result);
	return result;
}
}
