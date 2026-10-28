#pragma once
#include "movement.h"
namespace BodycamReference
{
inline double ReferenceMoveSpeed(bool crouched, bool aiming, bool sprint)
{
	const auto& row = TargetMovementSettings(aiming ? 2 : 1, crouched ? 1 : 0);
	return (!crouched && !aiming && sprint ? row.run : row.walk) * .01;
}
}
