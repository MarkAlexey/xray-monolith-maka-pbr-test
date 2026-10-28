#pragma once
#include "movement_vibration.h"
namespace BodycamReference
{
struct CameraActorPosition
{
	double x = 0, z = 0, floorY = 0, floorClearance = 0;
};
Vector HeadSocketWorldPosition(const CameraActorPosition&, double actorYaw, const Vector& socketMetres);
Quaternion WorldCameraQuaternion(const Rotation& parent, const Rotation& relative = {});
CameraPov CameraPovFromWorld(const Vector& eye, const Quaternion& worldCamera, double horizontalFov);
LocalPose WorldCameraPose(const CameraPov&);
Matrix CameraRelativePhysicalRoot(const Vector& unshakenEye, double actorYaw, const LocalPose& finalCamera);
Matrix HeadRelativeModelRoot(const Vector& finalSocket);
}
