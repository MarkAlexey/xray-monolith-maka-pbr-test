#pragma once
#include "engine_profile.h"
#include "equipment_asset.h"
namespace BodycamReference
{
// The rigid part of a pose matrix: normalised rotation, unit scale.
inline Matrix RigidPart(const Matrix& value)
{
	auto pose = Decompose(value);
	pose.rotation = NormalizeQuaternion(pose.rotation);
	pose.scale = { 1, 1, 1 };
	const auto result = Compose(pose);
	ValidateModelTransform(result);
	return result;
}
inline Matrix AutomaticModelRoot(const Matrix& referenceGun, const Matrix& engineGun)
{
	return Multiply(RigidPart(referenceGun), InverseAffine(RigidPart(engineGun)));
}
inline EquipmentTransforms AutomaticEquipment(const Matrix& cameraFromWeapon, const Matrix& cantedCameraFromWeapon)
{
	const auto aim = InverseAffine(RigidPart(cameraFromWeapon));
	const auto canted = InverseAffine(RigidPart(cantedCameraFromWeapon));
	LocalPose forward;
	forward.position = { 0, 0, 1 };
	// The ocular and lens entries stay identity: the renderer places the lens itself.
	const auto& identity = kIdentityMatrix;
	EquipmentTransforms result{ aim, canted, identity, identity, Multiply(aim, Compose(forward)), Multiply(canted, Compose(forward)) };
	ValidateEquipmentTransforms(result);
	return result;
}
}
