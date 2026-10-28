#pragma once
#include "transform.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
// Turns the elbow pole by the part of the gun's motion that rotates about the shoulder-to-wrist
// axis. The gun's translation and swing do not move the pole.
inline Vector ClassicArmSettlePole(
	const Vector& shoulder, const Vector& wrist, const Vector& pole, const Matrix& baselineGun, const Matrix& animatedGun)
{
	Vector axis{}, offset{};
	double length2 = 0;
	for (unsigned i = 0; i < 3; ++i)
	{
		axis[i] = wrist[i] - shoulder[i];
		offset[i] = pole[i] - shoulder[i];
		length2 += axis[i] * axis[i];
	}
	if (length2 < 1e-12)
		return pole;
	for (auto& v : axis)
		v /= std::sqrt(length2);
	auto q = Decompose(Multiply(animatedGun, InverseAffine(baselineGun))).rotation;
	// Shortest quaternion hemisphere, then swing/twist decomposition.
	if (q[3] < 0)
		for (auto& v : q)
			v = -v;
	const double projection = q[0] * axis[0] + q[1] * axis[1] + q[2] * axis[2];
	const double norm = std::hypot(projection, q[3]);
	if (!std::isfinite(norm) || norm < 1e-8)
		return pole;
	// Fold twist continuously into the native bend hemisphere. Ordinary small
	// twists are unchanged; crossing 90 degrees never jumps back to neutral.
	const double sine = std::clamp(2 * projection * q[3] / (norm * norm), -1., 1.);
	const double halfAngle = .5 * std::asin(sine);
	const double along = std::sin(halfAngle);
	const Quaternion twist{ axis[0] * along, axis[1] * along, axis[2] * along, std::cos(halfAngle) };
	const auto rotated = RotateVector(offset, twist);
	return { shoulder[0] + rotated[0], shoulder[1] + rotated[1], shoulder[2] + rotated[2] };
}
}
