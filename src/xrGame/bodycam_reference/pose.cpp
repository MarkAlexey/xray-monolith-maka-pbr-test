// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "pose.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
Vector VectorInterpTo(const Vector& current, const Vector& target, double seconds, double speed)
{
	const float rate = float(speed);
	if (rate <= 0)
		return target;
	Vector delta;
	for (unsigned i = 0; i < 3; ++i)
		delta[i] = target[i] - current[i];
	if ((delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2] < float(.0001))
		return target;
	const double alpha = std::clamp(float(seconds) * rate, 0.f, 1.f);
	Vector result;
	for (unsigned i = 0; i < 3; ++i)
		result[i] = delta[i] * alpha + current[i];
	return result;
}
namespace
{
const double Small = static_cast<float>(1e-8);
Vector Sub(const Vector& a, const Vector& b)
{
	return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
Vector Mul(const Vector& a, double b)
{
	return { a[0] * b, a[1] * b, a[2] * b };
}
Vector Cross(const Vector& a, const Vector& b)
{
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
double Square(const Vector& a)
{
	return (a[1] * a[1] + a[0] * a[0]) + a[2] * a[2];
}
Vector Normalize(const Vector& a)
{
	const double s = Square(a);
	return s == 1 ? a : s < Small ? Vector{} : Mul(a, 1 / std::sqrt(s));
}
Vector SecondBestAxis(const Vector& n)
{
	const Vector seed = std::abs(n[2]) > std::abs(n[0]) && std::abs(n[2]) > std::abs(n[1]) ? Vector{ 1, 0, 0 } : Vector{ 0, 0, 1 };
	const double dot = (n[0] * seed[0] + n[1] * seed[1]) + n[2] * seed[2];
	return Cross(Normalize(Sub(seed, Mul(n, dot))), n);
}
}
Vector NormalizeArmVector(const Vector& value)
{
	return Normalize(value);
}
double ArmSegmentLength(const Vector& from, const Vector& to)
{
	return std::sqrt(Square(Sub(to, from)));
}
ArmSolution SolveArm(const Vector& root, const Vector& target, const Vector& pole, double upperLength, double lowerLength)
{
	const auto delta = Sub(target, root);
	const double rawDistance = std::sqrt(Square(delta));
	const double distance = rawDistance < .0001 ? .0001 : rawDistance;
	const Vector direction = rawDistance < .0001 ? Vector{ 1, 0, 0 } : Normalize(delta);
	const auto poleDelta = Sub(pole, root);
	Vector bend;
	PoleBranch branch;
	if (Square(poleDelta) < 1e-8)
	{
		bend = { 0, 1, 0 };
		branch = PoleBranch::Zero;
	}
	else
	{
		const auto normal = Cross(direction, poleDelta);
		const double s = (normal[0] * normal[0] + normal[1] * normal[1]) + normal[2] * normal[2];
		if (s < 1e-8)
		{
			bend = SecondBestAxis(direction);
			branch = PoleBranch::Collinear;
		}
		else
		{
			const double dot = (direction[1] * poleDelta[1] + direction[0] * poleDelta[0]) + direction[2] * poleDelta[2];
			bend = Sub(poleDelta, Mul(direction, dot));
			const double length = Square(bend);
			if (length > Small)
				bend = Mul(bend, 1 / std::sqrt(length));
			branch = PoleBranch::Projected;
		}
	}
	ArmSolution result{ {}, target, rawDistance, distance, branch, false };
	const double total = upperLength + lowerLength;
	if (distance >= total)
	{
		for (int i = 0; i < 3; ++i)
		{
			result.joint[i] = root[i] + direction[i] * upperLength;
			result.end[i] = root[i] + direction[i] * total;
		}
		result.extended = true;
		return result;
	}
	const double denominator = (upperLength + upperLength) * distance;
	const double cosine =
		denominator == 0 ? 0 : ((distance * distance + upperLength * upperLength) - lowerLength * lowerLength) / denominator;
	const double perpendicular = std::sin(std::acos(std::clamp(cosine, -1.0, 1.0))) * upperLength;
	const double residual = upperLength * upperLength - perpendicular * perpendicular;
	double along = residual > 0 ? std::sqrt(residual) : 0;
	if (denominator != 0 && cosine < 0)
		along = -along;
	for (int i = 0; i < 3; ++i)
		result.joint[i] = (direction[i] * along + root[i]) + bend[i] * perpendicular;
	return result;
}
Quaternion BetweenNormals(const Vector& a, const Vector& b)
{
	double w = ((b[0] * a[0] + b[1] * a[1]) + b[2] * a[2]) + 1;
	Vector xyz;
	if (w >= static_cast<float>(1e-6))
		xyz = Cross(a, b);
	else
	{
		w = 0;
		xyz = Cross(a, std::abs(a[0]) > std::abs(a[1]) && std::abs(a[0]) > std::abs(a[2]) ? Vector{ 0, 1, 0 } : Vector{ -1, -0.0, -0.0 });
	}
	const double s = (w * w + xyz[1] * xyz[1]) + (xyz[2] * xyz[2] + xyz[0] * xyz[0]);
	if (s < Small)
		return { 0, 0, 0, 1 };
	const double inverse = 1 / std::sqrt(s);
	return { xyz[0] * inverse, xyz[1] * inverse, xyz[2] * inverse, w * inverse };
}
LocalPose BlendSkeletalControl(const LocalPose& original, const LocalPose& target, double alpha)
{
	const float a = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
	if (a <= static_cast<float>(1e-5))
		return original;
	if (a >= static_cast<float>(.99999))
		return target;
	const double wa = static_cast<float>(1.0 - a), wb = 1 - wa;
	if (wa <= static_cast<float>(1e-5))
		return target;
	if (wa >= static_cast<float>(.99999))
		return original;
	const auto& x = original.rotation;
	const auto& y = target.rotation;
	const double dot = (x[0] * y[0] + x[2] * y[2]) + (x[1] * y[1] + x[3] * y[3]);
	LocalPose result;
	for (int i = 0; i < 4; ++i)
		result.rotation[i] = (dot >= 0 ? y[i] : -y[i]) * wb + x[i] * wa;
	auto& q = result.rotation;
	const double n = (q[0] * q[0] + q[2] * q[2]) + (q[1] * q[1] + q[3] * q[3]);
	if (n >= 1e-8)
	{
		for (auto& v : q)
			v /= std::sqrt(n);
	}
	else
		q = { 0, 0, 0, 1 };
	for (int i = 0; i < 3; ++i)
	{
		result.position[i] = target.position[i] * wb + original.position[i] * wa;
		result.scale[i] = target.scale[i] * wb + original.scale[i] * wa;
	}
	return result;
}
}
