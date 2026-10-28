#pragma once
#include "constants.h"
#include "pose.h"
#include <vector>

namespace BodycamReference
{
// Reference exported skeletal basis, metres; column vectors, column-major.
// Convert explicitly at the X-Ray publication boundary, never by memcpy.
using Matrix = std::array<double, 16>;
inline constexpr Matrix kIdentityMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
// Source vectors are centimetres with z up, reference vectors metres with y up.
inline Vector SourceToReference(const Vector& centimetres)
{
	return { centimetres[0] * kMetresPerCentimetre, centimetres[2] * kMetresPerCentimetre, centimetres[1] * kMetresPerCentimetre };
}
inline Vector ReferenceToSource(const Vector& metres)
{
	return { metres[0] * kCentimetresPerMetre, metres[2] * kCentimetresPerMetre, metres[1] * kCentimetresPerMetre };
}
// Swapping y and z changes handedness, so the rotation axis is also negated.
inline Quaternion SourceToReference(const Quaternion& q)
{
	return { -q[0], -q[2], -q[1], q[3] };
}
inline Vector Translation(const Matrix& matrix)
{
	return { matrix[12], matrix[13], matrix[14] };
}
inline Quaternion Conjugate(const Quaternion& q)
{
	return { -q[0], -q[1], -q[2], q[3] };
}
Quaternion QuaternionProduct(const Quaternion& left, const Quaternion& right);
Quaternion NormalizeQuaternion(Quaternion value);
Quaternion SlerpQuaternion(const Quaternion& from, const Quaternion& to, double alpha);
Vector RotateVector(const Vector& value, const Quaternion& rotation);
Matrix Compose(const LocalPose& pose);
LocalPose Decompose(const Matrix& matrix);
Quaternion RotationMatrixQuaternion(const Matrix& rotationMatrix);
Matrix Multiply(const Matrix& left, const Matrix& right);
Matrix InverseAffine(const Matrix& matrix);
// Accumulates parent-first local poses into component-space matrices.
inline void ComponentMatrices(const std::vector<LocalPose>& locals, const std::vector<int>& parents, std::vector<Matrix>& result)
{
	result.clear();
	result.reserve(locals.size());
	for (unsigned i = 0; i < locals.size(); ++i)
	{
		const auto local = Compose(locals[i]);
		result.push_back(parents[i] < 0 ? local : Multiply(result[parents[i]], local));
	}
}
inline std::vector<Matrix> ComponentMatrices(const std::vector<LocalPose>& locals, const std::vector<int>& parents)
{
	std::vector<Matrix> result;
	ComponentMatrices(locals, parents, result);
	return result;
}
Matrix InverseMatrix(const Matrix& matrix);
}
