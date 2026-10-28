#include "spring.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
Quaternion QuaternionProduct(const Quaternion& a, const Quaternion& b)
{
	const auto x = a[0], y = a[1], z = a[2], w = a[3], bx = b[0], by = b[1], bz = b[2], bw = b[3];
	return { x * bw + w * bx + y * bz - z * by, y * bw + w * by + z * bx - x * bz, z * bw + w * bz + x * by - y * bx,
		w * bw - x * bx - y * by - z * bz };
}
Quaternion NormalizeQuaternion(Quaternion q)
{
	const double length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (length == 0)
		return { 0, 0, 0, 1 };
	const double inverse = 1 / length;
	for (auto& v : q)
		v *= inverse;
	return q;
}
Vector RotateVector(const Vector& v, const Quaternion& q)
{
	const double tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
	return { v[0] + q[3] * tx + q[1] * tz - q[2] * ty, v[1] + q[3] * ty + q[2] * tx - q[0] * tz, v[2] + q[3] * tz + q[0] * ty - q[1] * tx };
}
Quaternion SlerpQuaternion(const Quaternion& from, const Quaternion& to, double alpha)
{
	Quaternion target = to;
	double dot = from[0] * to[0] + from[1] * to[1] + from[2] * to[2] + from[3] * to[3];
	if (dot < 0)
	{
		for (auto& v : target)
			v = -v;
		dot = -dot;
	}
	double weight = 1 - alpha;
	// Nearly parallel rotations are blended linearly and renormalized.
	const bool spherical = dot < .9995;
	if (spherical)
	{
		const double angle = std::acos(dot), sine = std::sin(angle);
		weight = std::sin(weight * angle) / sine;
		alpha = std::sin(alpha * angle) / sine;
	}
	Quaternion result;
	for (unsigned i = 0; i < 4; ++i)
		result[i] = from[i] * weight + target[i] * alpha;
	return spherical ? result : NormalizeQuaternion(result);
}
namespace
{
double Determinant3(const Matrix& m)
{
	return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}
}
Matrix Compose(const LocalPose& p)
{
	const auto x = p.rotation[0], y = p.rotation[1], z = p.rotation[2], w = p.rotation[3];
	const auto x2 = x + x, y2 = y + y, z2 = z + z;
	const auto xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2, wx = w * x2, wy = w * y2, wz = w * z2;
	const auto sx = p.scale[0], sy = p.scale[1], sz = p.scale[2];
	return { (1 - (yy + zz)) * sx, (xy + wz) * sx, (xz - wy) * sx, 0, (xy - wz) * sy, (1 - (xx + zz)) * sy, (yz + wx) * sy, 0,
		(xz + wy) * sz, (yz - wx) * sz, (1 - (xx + yy)) * sz, 0, p.position[0], p.position[1], p.position[2], 1 };
}
LocalPose Decompose(const Matrix& m)
{
	LocalPose p;
	p.position = { m[12], m[13], m[14] };
	for (int column = 0; column < 3; ++column)
	{
		const int c = 4 * column;
		p.scale[column] = std::sqrt(m[c] * m[c] + m[c + 1] * m[c + 1] + m[c + 2] * m[c + 2]);
	}
	if (Determinant3(m) < 0)
		p.scale[0] = -p.scale[0];
	if (p.scale[0] == 0 || p.scale[1] == 0 || p.scale[2] == 0)
		throw std::invalid_argument("Nonzero skeletal scale required");
	const double sx = 1 / p.scale[0], sy = 1 / p.scale[1], sz = 1 / p.scale[2];
	p.rotation = RotationMatrixQuaternion(
		{ m[0] * sx, m[1] * sx, m[2] * sx, 0, m[4] * sy, m[5] * sy, m[6] * sy, 0, m[8] * sz, m[9] * sz, m[10] * sz, 0, 0, 0, 0, 1 });
	return p;
}
Quaternion RotationMatrixQuaternion(const Matrix& m)
{
	const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], g = m[9], h = m[2], i = m[6], j = m[10];
	const double trace = a + e + j;
	if (trace > 0)
	{
		const double s = .5 / std::sqrt(trace + 1);
		return { (i - g) * s, (c - h) * s, (d - b) * s, .25 / s };
	}
	if (a > e && a > j)
	{
		const double s = 2 * std::sqrt(1 + a - e - j);
		return { .25 * s, (b + d) / s, (c + h) / s, (i - g) / s };
	}
	if (e > j)
	{
		const double s = 2 * std::sqrt(1 + e - a - j);
		return { (b + d) / s, .25 * s, (g + i) / s, (c - h) / s };
	}
	const double s = 2 * std::sqrt(1 + j - a - e);
	return { (c + h) / s, (g + i) / s, .25 * s, (d - b) / s };
}
Matrix Multiply(const Matrix& a, const Matrix& b)
{
	Matrix out{};
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
	return out;
}
Matrix InverseMatrix(const Matrix& m)
{
	// Inverse by 2x2 cofactors; aCR is column C, row R. The operation order is part of the bit-exact result,
	// and a singular matrix yields the zero matrix.
	const double a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3], a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7], a20 = m[8], a21 = m[9],
				 a22 = m[10], a23 = m[11], a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
	const double b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11,
				 b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30,
				 b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
	const double determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	if (determinant == 0)
		return {};
	const double inverse = 1 / determinant;
	return { (a11 * b11 - a12 * b10 + a13 * b09) * inverse, (a02 * b10 - a01 * b11 - a03 * b09) * inverse,
		(a31 * b05 - a32 * b04 + a33 * b03) * inverse, (a22 * b04 - a21 * b05 - a23 * b03) * inverse,
		(a12 * b08 - a10 * b11 - a13 * b07) * inverse, (a00 * b11 - a02 * b08 + a03 * b07) * inverse,
		(a32 * b02 - a30 * b05 - a33 * b01) * inverse, (a20 * b05 - a22 * b02 + a23 * b01) * inverse,
		(a10 * b10 - a11 * b08 + a13 * b06) * inverse, (a01 * b08 - a00 * b10 - a03 * b06) * inverse,
		(a30 * b04 - a31 * b02 + a33 * b00) * inverse, (a21 * b02 - a20 * b04 - a23 * b00) * inverse,
		(a11 * b07 - a10 * b09 - a12 * b06) * inverse, (a00 * b09 - a01 * b07 + a02 * b06) * inverse,
		(a31 * b01 - a30 * b03 - a32 * b00) * inverse, (a20 * b03 - a21 * b01 + a22 * b00) * inverse };
}
Matrix InverseAffine(const Matrix& m)
{
	const double determinant = Determinant3(m);
	if (determinant == 0)
		throw std::invalid_argument("Invertible skeletal parent required");
	const double inv = 1 / determinant;
	Matrix out{ (m[5] * m[10] - m[9] * m[6]) * inv, (m[9] * m[2] - m[1] * m[10]) * inv, (m[1] * m[6] - m[5] * m[2]) * inv, 0,
		(m[8] * m[6] - m[4] * m[10]) * inv, (m[0] * m[10] - m[8] * m[2]) * inv, (m[4] * m[2] - m[0] * m[6]) * inv, 0,
		(m[4] * m[9] - m[8] * m[5]) * inv, (m[8] * m[1] - m[0] * m[9]) * inv, (m[0] * m[5] - m[4] * m[1]) * inv, 0, 0, 0, 0, 1 };
	for (int r = 0; r < 3; ++r)
		out[12 + r] = -(out[r] * m[12] + out[4 + r] * m[13] + out[8 + r] * m[14]);
	return out;
}
namespace
{
Matrix ApplySpringControl(const Matrix& current, const Matrix& delta, int space, const Matrix& parent, double alpha)
{
	const float weight = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
	if (weight <= kBlendEpsilon)
		return current;
	auto p = Decompose(current);
	const auto d = Decompose(delta);
	Vector translation = d.position;
	if (space == kBoneSpace)
	{
		for (int i = 0; i < 3; ++i)
			translation[i] *= p.scale[i];
		translation = RotateVector(translation, p.rotation);
		p.rotation = NormalizeQuaternion(QuaternionProduct(p.rotation, d.rotation));
	}
	else
		p.rotation = NormalizeQuaternion(QuaternionProduct(d.rotation, p.rotation));
	for (int i = 0; i < 3; ++i)
	{
		p.position[i] += translation[i];
		p.scale[i] *= d.scale[i];
	}
	const Matrix target = Compose(p);
	if (weight >= kBlendFull)
		return target;
	const Matrix inverse = InverseAffine(parent);
	const auto blended = BlendSkeletalControl(Decompose(Multiply(inverse, current)), Decompose(Multiply(inverse, target)), weight);
	return Multiply(parent, Compose(blended));
}
}
Matrix ApplySpringStack(const Matrix& current, const Matrix& parent, const std::array<SwayNode, 10>& nodes, const Vector& pivotCm)
{
	Matrix result = current;
	const Vector pivot = SourceToReference(pivotCm);
	for (int index : SwayRig::kGraphOrder)
	{
		const auto& node = nodes[index];
		LocalPose delta;
		if (node.axis == SwayAxis::X)
			delta.position[0] = double(node.value) * kMetresPerCentimetre;
		else if (node.axis == SwayAxis::Z)
			delta.position[1] = double(node.value) * kMetresPerCentimetre;
		else
		{
			const double half = (double(node.value) * kPi / 180 * (node.axis == SwayAxis::Yaw ? -1 : 1)) / 2;
			const double sine = std::sin(half);
			delta.rotation = { 0, 0, 0, std::cos(half) };
			delta.rotation[node.axis == SwayAxis::Roll ? 0 : node.axis == SwayAxis::Yaw ? 1 : 2] = sine;
			delta.position = RotateVector(pivot, delta.rotation);
			for (int i = 0; i < 3; ++i)
				delta.position[i] -= pivot[i];
		}
		result = ApplySpringControl(result, Compose(delta), node.targetSpace, parent, node.alpha);
	}
	return result;
}
}
