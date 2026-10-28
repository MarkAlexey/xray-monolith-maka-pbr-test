// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_pose.h"
#include "constants.h"
#include "transform.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
namespace
{
// |test| above this is treated as looking straight up or down.
constexpr double kPoleTest = .4999995;
double HalfAngle(double a)
{
	return (a - std::trunc(a / 360) * 360) * (kRadiansPerDegree / 2);
}
double Atan2(double y, double x)
{
	return y == 0 && x == 0 ? 0 : std::atan2(y, x);
}
}
Quaternion RecoilQuaternion(const Rotation& r)
{
	const double p = HalfAngle(r.pitch), y = HalfAngle(r.yaw), l = HalfAngle(r.roll);
	const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(l), cr = std::cos(l);
	return { ((sy * sp) * cr) + -((cy * cp) * sr), -((cy * sp) * cr) + -((sy * cp) * sr), -((cy * sp) * sr) + ((sy * cp) * cr),
		((sy * sp) * sr) + ((cy * cp) * cr) };
}
Rotation RecoilRotation(const Quaternion& q)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3], rad = kDegreesPerRadian;
	const double test = x * z - y * w, xy = x * y, wz = w * z, yy = y * y, zz = z * z;
	Rotation r{};
	r.yaw = Atan2((xy + xy) + (wz + wz), 1 - ((yy + yy) + (zz + zz))) * rad;
	if (test < -kPoleTest || test > kPoleTest)
	{
		r.pitch = test < -kPoleTest ? -90 : 90;
		const double angle = (r.pitch < 0 ? -r.yaw : r.yaw) - Atan2(x, w) * (2 * kDegreesPerRadian);
		r.roll = std::fmod(angle, 360);
		if (r.roll < 0)
			r.roll += 360;
		if (r.roll > 180)
			r.roll -= 360;
	}
	else
	{
		r.pitch = std::asin(std::clamp(test + test, -1., 1.)) * rad;
		const double yz = y * z, xw = x * w, xx = x * x;
		r.roll = Atan2((yz * -2) - (xw + xw), 1 - ((xx + xx) + (yy + yy))) * rad;
	}
	return r;
}
Quaternion ProceduralYXZQuaternion(const Rotation& r)
{
	return SourceToReference(RecoilQuaternion({ r.roll, r.yaw, r.pitch }));
}
RecoilPose ComposeRecoilSway(const Rotation& base, const Rotation& sway, const Vector& pivot)
{
	const auto a = RecoilQuaternion(base), b = RecoilQuaternion(sway);
	const Quaternion q{ a[0] * b[3] + a[3] * b[0] + a[1] * b[2] - a[2] * b[1], a[1] * b[3] + a[3] * b[1] + a[2] * b[0] - a[0] * b[2],
		a[2] * b[3] + a[3] * b[2] + a[0] * b[1] - a[1] * b[0], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] };
	// Rotates pivot by b: pivot + w t + cross(b, t) with t = 2 cross(b, pivot).
	const Vector t{ 2 * (b[1] * pivot[2] - b[2] * pivot[1]), 2 * (b[2] * pivot[0] - b[0] * pivot[2]),
		2 * (b[0] * pivot[1] - b[1] * pivot[0]) };
	Vector delta{ (pivot[0] + b[3] * t[0] + b[1] * t[2] - b[2] * t[1]) - pivot[0],
		(pivot[1] + b[3] * t[1] + b[2] * t[0] - b[0] * t[2]) - pivot[1], (pivot[2] + b[3] * t[2] + b[0] * t[1] - b[1] * t[0]) - pivot[2] };
	return { RecoilRotation(q), delta };
}
bool RecoilPoseCache::PreUpdate(const RecoilPose& pose, bool autoUpdate, bool componentValid, std::uint64_t publication)
{
	if (!autoUpdate || !componentValid)
		return false;
	rotation_ = RecoilQuaternion(pose.rotation);
	location_ = pose.location;
	publication_ = publication;
	++captures_;
	return true;
}
RecoilPoseSnapshot RecoilPoseCache::Evaluate() const
{
	return { { RecoilRotation(rotation_), location_ }, publication_, captures_ };
}
}
