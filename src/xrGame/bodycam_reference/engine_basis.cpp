#include "engine_basis.h"
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
// Reference camera local -Z forward; native camera local +Z forward.
const Matrix cameraBasis = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1 };
EngineRowMatrix Narrow(const Matrix& matrix)
{
	EngineRowMatrix result;
	for (unsigned i = 0; i < 16; ++i)
		result[i] = float(matrix[i]);
	return result;
}
Matrix Widen(const EngineRowMatrix& matrix)
{
	Matrix result;
	for (unsigned i = 0; i < 16; ++i)
		result[i] = matrix[i];
	return result;
}
Vector Direction(const Matrix& matrix, const Vector& v)
{
	return { matrix[0] * v[0] + matrix[4] * v[1] + matrix[8] * v[2], matrix[1] * v[0] + matrix[5] * v[1] + matrix[9] * v[2],
		matrix[2] * v[0] + matrix[6] * v[1] + matrix[10] * v[2] };
}
}
EngineBasis::EngineBasis(Matrix basis) : engineFromReference_(basis)
{
	for (double v : basis)
		if (!std::isfinite(v))
			throw std::invalid_argument("nonfinite engine coordinate basis");
	if (basis[3] != 0 || basis[7] != 0 || basis[11] != 0 || basis[12] != 0 || basis[13] != 0 || basis[14] != 0 || basis[15] != 1)
		throw std::invalid_argument("coordinate basis cannot contain translation or projection");
	for (unsigned a = 0; a < 3; ++a)
		for (unsigned b = 0; b < 3; ++b)
		{
			double dot = 0;
			for (unsigned i = 0; i < 3; ++i)
				dot += basis[a * 4 + i] * basis[b * 4 + i];
			if (std::abs(dot - (a == b ? 1. : 0.)) > 1e-8)
				throw std::invalid_argument("engine coordinate basis must be orthonormal");
		}
	referenceFromEngine_ = InverseAffine(basis);
}
Matrix EngineBasis::ToReference(const EngineRowMatrix& engine) const
{
	// Row-vector storage read as column-vector storage keeps the linear index; only the bases change.
	return Multiply(Multiply(referenceFromEngine_, Widen(engine)), engineFromReference_);
}
EngineRowMatrix EngineBasis::ToEngine(const Matrix& reference) const
{
	return Narrow(Multiply(Multiply(engineFromReference_, reference), referenceFromEngine_));
}
EngineRowMatrix EngineBasis::CameraWorldToEngine(const Matrix& reference) const
{
	return Narrow(Multiply(Multiply(engineFromReference_, reference), cameraBasis));
}
Matrix EngineBasis::CameraWorldToReference(const EngineRowMatrix& engine) const
{
	return Multiply(Multiply(referenceFromEngine_, Widen(engine)), cameraBasis);
}
EngineRowMatrix EngineBasis::CameraRelativeToEngine(const Matrix& reference) const
{
	return Narrow(Multiply(Multiply(cameraBasis, reference), referenceFromEngine_));
}
Vector EngineBasis::DirectionToEngine(const Vector& v) const
{
	return Direction(engineFromReference_, v);
}
Vector EngineBasis::DirectionToReference(const Vector& v) const
{
	return Direction(referenceFromEngine_, v);
}
}
