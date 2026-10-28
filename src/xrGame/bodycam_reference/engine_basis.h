#pragma once
#include "transform.h"

namespace BodycamReference
{
// X-Ray row-vector/row-major matrix; metre units, float publication boundary.
using EngineRowMatrix = std::array<float, 16>;
class EngineBasis
{
public:
	// Explicit orthonormal coordinate basis, including handedness reflection.
	// Model calibration/attachment transforms are separate from this basis.
	explicit EngineBasis(Matrix engineFromReference);
	Matrix ToReference(const EngineRowMatrix&) const;
	EngineRowMatrix ToEngine(const Matrix&) const;
	EngineRowMatrix CameraWorldToEngine(const Matrix&) const;
	Matrix CameraWorldToReference(const EngineRowMatrix&) const;
	EngineRowMatrix CameraRelativeToEngine(const Matrix&) const;
	Vector DirectionToEngine(const Vector&) const;
	Vector DirectionToReference(const Vector&) const;

private:
	Matrix engineFromReference_, referenceFromEngine_;
};
}
