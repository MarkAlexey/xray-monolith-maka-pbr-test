#pragma once
#include "animation_blend.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
// The wrist stays this far inside full extension and outside the fully folded arm, in metres.
constexpr double kReachMargin = .0001;
// Two more micrometres absorb the narrowing to the engine's float positions.
constexpr double kReachFloatGuard = .000002;
constexpr double kFoldedReachMargin = .000102;
constexpr unsigned kReachScanSteps = 16, kReachBisections = 24;
struct ClassicArmReach
{
	Vector shoulder{}, elbow{}, wrist{};
};
inline double ClassicReachDistance(const Vector& a, const Vector& b)
{
	return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

struct ClassicRigidLimit
{
	Matrix delta = kIdentityMatrix;
	double alpha = 0;
};
inline Vector ClassicTransformPoint(const Matrix& matrix, const Vector& point)
{
	return { matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
		matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
		matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14] };
}
struct ClassicReachBounds
{
	Vector shoulder{}, wrist{};
	double minimum = 0, maximum = 0;
};
inline ClassicReachBounds ClassicArmBounds(const ClassicArmReach& arm)
{
	const double upper = ClassicReachDistance(arm.shoulder, arm.elbow), lower = ClassicReachDistance(arm.elbow, arm.wrist);
	return { arm.shoulder, arm.wrist, std::abs(upper - lower) + kFoldedReachMargin,
		std::max(upper + lower - kReachMargin, kReachMargin) - kReachFloatGuard };
}
inline bool ClassicRigidReachable(const ClassicReachBounds& bounds, const Matrix& delta)
{
	const double distance = ClassicReachDistance(bounds.shoulder, ClassicTransformPoint(delta, bounds.wrist));
	return distance <= bounds.maximum && distance >= bounds.minimum;
}
inline bool ClassicRigidReachable(const ClassicArmReach& arm, const Matrix& delta)
{
	return ClassicRigidReachable(ClassicArmBounds(arm), delta);
}
inline ClassicRigidLimit LimitClassicRigidController(const Matrix& engineGun, const Matrix& desiredGun, const ClassicArmReach& right,
	const ClassicArmReach* left = nullptr, const Matrix& baselineDelta = kIdentityMatrix)
{
	const auto baseline = Decompose(baselineDelta);
	const auto desired = Decompose(Multiply(desiredGun, InverseAffine(engineGun)));
	// Arm lengths stay fixed throughout the scan and bisection.
	const auto rightBounds = ClassicArmBounds(right);
	const auto leftBounds = left ? ClassicArmBounds(*left) : ClassicReachBounds{};
	const auto at = [&](double alpha) { return Compose(BlendAnimationLocal(baseline, desired, alpha)); };
	const auto valid = [&](const Matrix& delta)
	{ return ClassicRigidReachable(rightBounds, delta) && (!left || ClassicRigidReachable(leftBounds, delta)); };
	ClassicRigidLimit result{ baselineDelta, 0 };
	// A baseline that is already out of reach is returned as it is: only the movement beyond it is limited.
	if (!valid(result.delta))
		return result;
	// Scan the path in equal steps, then bisect between the last reachable step and the first
	// unreachable one. Reachability is checked at the sampled points only.
	double low = 0, high = 1;
	for (unsigned step = 1; step <= kReachScanSteps; ++step)
	{
		const double fraction = step / double(kReachScanSteps);
		const auto delta = at(fraction);
		if (!valid(delta))
		{
			high = fraction;
			break;
		}
		low = fraction;
		result = { delta, fraction };
	}
	if (low == 1)
		return result;
	for (unsigned iteration = 0; iteration < kReachBisections; ++iteration)
	{
		const double middle = (low + high) * .5;
		const auto delta = at(middle);
		if (valid(delta))
		{
			low = middle;
			result = { delta, middle };
		}
		else
			high = middle;
	}
	return result;
}
}
