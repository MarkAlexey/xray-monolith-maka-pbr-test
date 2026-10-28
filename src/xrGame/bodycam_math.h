#pragma once

#include "bodycam_reference/engine_basis.h"

namespace Bodycam
{
void BuildCameraBasis(float yaw, float pitch, Fvector& dir, Fvector& up, Fvector& right);

// Engine matrices and the Reference library's matrices hold the same sixteen
// elements in the same order; these copy them without a change of basis.
BodycamReference::Matrix ToPortable(const Fmatrix& matrix);
Fmatrix ToEngine(const BodycamReference::Matrix& matrix);
// The float rows BodycamReference::EngineBasis converts.
BodycamReference::EngineRowMatrix ToRows(const Fmatrix& matrix);
Fmatrix FromRows(const BodycamReference::EngineRowMatrix& rows);

// A HUD attach offset as the weapon configs give it: metres, and heading, pitch and bank in degrees.
inline Fmatrix AttachTransform(const Fvector& position, const Fvector& degrees)
{
	Fmatrix result;
	result.setHPB(deg2rad(degrees.x), deg2rad(degrees.y), deg2rad(degrees.z));
	result.translate_over(position);
	return result;
}
} // namespace Bodycam
