#include "stdafx.h"
#include "bodycam_math.h"

#include <cstring>

namespace Bodycam
{
void BuildCameraBasis(float yaw, float pitch, Fvector& dir, Fvector& up, Fvector& right)
{
	dir.setHP(yaw, pitch);
	Fvector::generate_orthonormal_basis_normalized(dir, up, right);
}

BodycamReference::Matrix ToPortable(const Fmatrix& matrix)
{
	BodycamReference::Matrix result;
	for (unsigned i = 0; i < 4; ++i)
		for (unsigned j = 0; j < 4; ++j)
			result[i * 4 + j] = matrix.m[i][j];
	return result;
}

Fmatrix ToEngine(const BodycamReference::Matrix& matrix)
{
	Fmatrix result;
	for (unsigned i = 0; i < 4; ++i)
		for (unsigned j = 0; j < 4; ++j)
			result.m[i][j] = float(matrix[i * 4 + j]);
	return result;
}

BodycamReference::EngineRowMatrix ToRows(const Fmatrix& matrix)
{
	BodycamReference::EngineRowMatrix rows;
	std::memcpy(rows.data(), &matrix, sizeof(matrix));
	return rows;
}

Fmatrix FromRows(const BodycamReference::EngineRowMatrix& rows)
{
	Fmatrix result;
	std::memcpy(&result, rows.data(), sizeof(result));
	return result;
}
} // namespace Bodycam
