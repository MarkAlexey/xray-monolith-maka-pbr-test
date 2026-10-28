#pragma once
#include "camera_presentation.h"
#include <cmath>
namespace BodycamReference
{
inline CameraActorPosition CameraActorAnchor(CameraActorPosition actor, double yaw, const Matrix& referenceCameraPoint)
{
	// The engine body and eye share the actor's planar origin. Remove only the imported mesh's
	// static socket offset and keep every animated delta.
	const double c = std::cos(yaw), s = std::sin(yaw);
	actor.x -= c * referenceCameraPoint[12] - s * referenceCameraPoint[14];
	actor.z -= s * referenceCameraPoint[12] + c * referenceCameraPoint[14];
	return actor;
}
}
