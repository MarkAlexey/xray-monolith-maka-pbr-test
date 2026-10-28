#pragma once
#include <array>

namespace BodycamReference
{
using Vector = std::array<double, 3>;
using Quaternion = std::array<double, 4>;
Vector VectorInterpTo(const Vector& current, const Vector& target, double seconds, double speed = 5);
struct LocalPose
{
	Vector position{};
	Quaternion rotation{ { 0, 0, 0, 1 } };
	Vector scale{ { 1, 1, 1 } };
};
enum class PoleBranch
{
	Zero,
	Collinear,
	Projected
};
struct ArmSolution
{
	Vector joint, end;
	double rawDistance, distance;
	PoleBranch branch;
	bool extended;
};
// Positions and lengths in centimetres.
ArmSolution SolveArm(const Vector& root, const Vector& target, const Vector& pole, double upperLength, double lowerLength);
Quaternion BetweenNormals(const Vector& original, const Vector& solved);
Vector NormalizeArmVector(const Vector& value);
double ArmSegmentLength(const Vector& from, const Vector& to);
LocalPose BlendSkeletalControl(const LocalPose& original, const LocalPose& target, double alpha);
}
