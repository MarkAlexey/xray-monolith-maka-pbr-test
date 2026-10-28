// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "ads_sway.h"
#include "scalar_math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
// A cubic curve key. weightMode bit 0 means the arrive tangent has a weight, bit 1 the leave tangent.
struct Key
{
	float time, value;
	int weightMode;
	float arriveTangent, arriveWeight, leaveTangent, leaveWeight;
};
constexpr Key kKeys[] = { { 0, .292273f, 3, -.021840246f, .8432252f, .22434849f, .010713874f },
	{ .2318081f, .3616377f, 3, .504548f, .06973028f, .504548f, .06973028f }, { .46808064f, .55662984f, 0, 1.2426277f, 0, 1.2426277f, 0 },
	{ 1, 1, 3, .07247636f, .23598874f, -.02953722f, .1722678f } };
// The z scale at curve time 0; it rises linearly to 1.
constexpr float kMinDepthScale = .39510345f;
// A control change is limited to this many degrees per frame before it becomes a velocity.
constexpr float kMaxControlStepDegrees = 200;
constexpr double kVelocityInterpSpeed = 25;
// Yaw rate in degrees per second at which the scale curve reaches its end.
constexpr double kFullSwayYawRate = 120;
// Single-precision sine and cosine by range reduction and minimax polynomials, as Unreal's FMath::SinCos computes them.
std::array<float, 2> SinCos(float angle)
{
	float q = angle * .15915493667125702f;
	q = float(std::trunc(double(angle >= 0 ? q + .5f : q - .5f)));
	float x = angle - q * 6.2831854820251465f, sign = 1;
	if (x > 1.5707963705062866f)
	{
		x = 3.1415927410125732f - x;
		sign = -1;
	}
	else if (x < -1.5707963705062866f)
	{
		x = -3.1415927410125732f - x;
		sign = -1;
	}
	const float z = x * x;
	float s = 2.7525561563379597e-6f - z * 2.388985897994189e-8f;
	s = s * z - .00019840874301735312f;
	s = s * z + .008333330973982811f;
	s = s * z - .1666666716337204f;
	s = (s * z + 1) * x;
	float c = 2.47604948526714e-5f - z * 2.605161455448979e-7f;
	c = c * z - .0013888378161936998f;
	c = c * z + .04166663810610771f;
	c = c * z - .5f;
	c = (c * z + 1) * sign;
	return { s, c };
}
double SignedPow(double x)
{
	return x > 0 ? std::pow(x, 1. / 3.) : x < 0 ? -std::pow(-x, 1. / 3.) : 0;
}
struct CurveRoots
{
	std::array<double, 3> values;
	unsigned count;
};
CurveRoots Roots(const std::array<double, 4>& c)
{
	const double inv = 1 / (c[3] == 0 ? 1.0000000000000001e-16 : c[3]);
	const double A = inv * c[2], C = inv * c[0], B = inv * c[1], square = A * A, shift = A * (1. / 3.);
	const double p = B * (1. / 3.) - square * (1. / 9.);
	const double unhalved = ((A * (2. / 27.)) * square - shift * B) + C;
	const double q = unhalved * .5, p3 = (p * p) * p, D = (unhalved * unhalved) * .25 + p3;
	CurveRoots roots;
	if (std::abs(D) <= 1e-8)
	{
		if (std::abs(q) <= 1e-8)
			roots = { { 0 }, 1 };
		else
		{
			const double u = SignedPow(-q);
			roots = { { u + u, -u }, 2 };
		}
	}
	else if (D < 0)
	{
		const double phi = std::acos((-1 / std::sqrt(-p3)) * q) * (1. / 3.), t = std::sqrt(-p) * 2;
		roots = { { std::cos(phi) * t, -(std::cos(phi + 1.0471975511965976) * t), -(std::cos(phi - 1.0471975511965976) * t) }, 3 };
	}
	else
	{
		const double d = std::sqrt(D);
		roots = { { SignedPow(d - q) - SignedPow(d + q) }, 1 };
	}
	for (unsigned i = 0; i < roots.count; ++i)
		roots.values[i] -= shift;
	return roots;
}
float Segment(const Key& left, const Key& right, float time)
{
	const float t0 = left.time, v0 = left.value, t1 = right.time, v1 = right.value, dt = t1 - t0;
	if (dt <= 0)
		return v0;
	const float alpha = (time - t0) / dt, dt2 = dt * dt;
	const auto a0 = SinCos(float(std::atan(double(left.leaveTangent)))), a1 = SinCos(float(std::atan(double(right.arriveTangent))));
	const float d0 = dt * left.leaveTangent, d1 = dt * right.arriveTangent;
	const float weight0 = left.weightMode > 1 ? left.leaveWeight : float(std::sqrt(double(d0 * d0 + dt2))) * float(1. / 3.);
	const float weight1 = (right.weightMode & ~2) != 0 ? right.arriveWeight : float(std::sqrt(double(d1 * d1 + dt2))) * float(1. / 3.);
	const float dx0 = weight0 * a0[1], dy0 = weight0 * a0[0], dx1 = weight1 * a1[1], dy1 = weight1 * a1[0];
	const float x0 = t0 + dx0, x1 = t1 - dx1, y0 = v0 + dy0, y1 = v1 - dy1, inv = 1.f / dt;
	const double a = float((x0 - t0) * inv), b = float((x1 - t0) * inv), d = b - a, e = d - a;
	const auto roots = Roots({ -double(alpha), 3 * a, 3 * e, ((1 - b) - d) - e });
	// A double root yields two candidates. That case is not reproduced.
	if (roots.count == 2)
		throw std::domain_error("ADS sway curve has a double root");
	// Of three roots, the largest inside [0, 1].
	std::optional<double> selected;
	if (roots.count == 1)
		selected = roots.values[0];
	else
		for (unsigned i = 0; i < roots.count; ++i)
		{
			const double r = roots.values[i];
			if ((r > 0 || std::abs(r) <= 1e-8) && (r < 1 || std::abs(r - 1) <= 1e-8) && (!selected || *selected < 0 || r > *selected))
				selected = r;
		}
	const float u = float(selected.value_or(0));
	const float l0 = dy0 * u + v0, l1 = (y1 - y0) * u + y0, l2 = dy1 * u + y1;
	const float p0 = (l1 - l0) * u + l0, p1 = (l2 - l1) * u + l1;
	return (p1 - p0) * u + p0;
}
}
Vector AdsSwayScale(double time)
{
	const float t = float(time);
	float y;
	if (t < 0)
		y = kKeys[0].value;
	else if (t > 1)
		y = kKeys[3].value;
	else
	{
		unsigned i = 0;
		while (i < 2 && t >= kKeys[i + 1].time)
			++i;
		y = Segment(kKeys[i], kKeys[i + 1], t);
	}
	const float z = t < 0 ? kMinDepthScale : t > 1 ? 1.f : (1.f - kMinDepthScale) * t + kMinDepthScale;
	return { 0, y + 0.f, z + 0.f };
}
namespace
{
Rotation InterpolateVelocity(const Rotation& current, const Rotation& target, double seconds, double speed)
{
	const float dt = float(seconds), rate = float(speed);
	if (dt == 0 || (current.pitch == target.pitch && current.yaw == target.yaw && current.roll == target.roll))
		return current;
	if (rate <= 0)
		return target;
	const Rotation delta{ WrapDegrees(target.pitch - current.pitch), WrapDegrees(target.yaw - current.yaw),
		WrapDegrees(target.roll - current.roll) };
	if (std::abs(WrapDegrees(delta.pitch)) <= float(.0001) && std::abs(WrapDegrees(delta.yaw)) <= float(.0001) &&
		std::abs(WrapDegrees(delta.roll)) <= float(.0001))
		return target;
	const double alpha = std::clamp(dt * rate, 0.f, 1.f);
	return { WrapDegrees(current.pitch + delta.pitch * alpha), WrapDegrees(current.yaw + delta.yaw * alpha),
		WrapDegrees(current.roll + delta.roll * alpha) };
}
}
const Vector& AdsSwayProducer::Update(const Rotation& current, double dt, bool valid, bool curve)
{
	if (!valid)
		return scale_;
	const double inverse = float(dt == 0 ? 0 : 1 / dt);
	const auto axis = [inverse](double delta)
	{ return double(std::clamp(float(WrapDegrees(delta)), -kMaxControlStepDegrees, kMaxControlStepDegrees)) * inverse; };
	const Rotation target{ axis(current.pitch - previous_.pitch), axis(current.yaw - previous_.yaw), axis(current.roll - previous_.roll) };
	velocity_ = InterpolateVelocity(velocity_, target, float(dt), kVelocityInterpSpeed);
	previous_ = current;
	if (curve)
	{
		time_ = float(std::min(std::abs(double(float(velocity_.yaw))) / kFullSwayYawRate, 1.));
		scale_ = AdsSwayScale(time_);
	}
	return scale_;
}
}
