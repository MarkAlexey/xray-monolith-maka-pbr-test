// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "camera_presentation.h"
#include "constants.h"
#include "recoil_pose.h"
#include <cmath>
namespace BodycamReference
{
Vector HeadSocketWorldPosition(const CameraActorPosition& actor, double yaw, const Vector& socket)
{
	const double c = std::cos(yaw), s = std::sin(yaw);
	return { actor.x + c * socket[0] - s * socket[2], actor.floorY + actor.floorClearance + .9 + (-.92) + socket[1],
		actor.z + s * socket[0] + c * socket[2] };
}
Quaternion WorldCameraQuaternion(const Rotation& parent, const Rotation& relative)
{
	const auto q = QuaternionProduct(RecoilQuaternion(parent), RecoilQuaternion(relative));
	return NormalizeQuaternion(QuaternionProduct({ q[1], -q[2], -q[0], q[3] }, { 0, 1, 0, 0 }));
}
CameraPov CameraPovFromWorld(const Vector& eye, const Quaternion& worldCamera, double fov)
{
	const auto q = QuaternionProduct(worldCamera, { 0, -1, 0, 0 });
	return { { eye[2] * kCentimetresPerMetre, -eye[0] * kCentimetresPerMetre, eye[1] * kCentimetresPerMetre },
		RecoilRotation({ -q[2], q[0], -q[1], q[3] }), fov };
}
LocalPose WorldCameraPose(const CameraPov& pov)
{
	LocalPose pose;
	pose.position = { -pov.location[1] / kCentimetresPerMetre, pov.location[2] / kCentimetresPerMetre,
		pov.location[0] / kCentimetresPerMetre };
	pose.rotation = WorldCameraQuaternion(pov.rotation);
	return pose;
}
Matrix CameraRelativePhysicalRoot(const Vector& eye, double yaw, const LocalPose& camera)
{
	const auto cross = [](const Vector& a, const Vector& b) -> Vector
	{ return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; };
	const auto normalize = [](Vector v)
	{
		const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		const double inverse = 1 / (length == 0 ? 1 : length);
		for (auto& x : v)
			x *= inverse;
		return v;
	};
	const Vector direction{ -std::sin(yaw), 0, std::cos(yaw) };
	const auto right = normalize(cross(direction, { 0, 1, 0 })), up = normalize(cross(right, direction));
	// eye - (eye + direction) is not -direction in floating point: the sum rounds at large world coordinates.
	Vector z;
	for (unsigned i = 0; i < 3; ++i)
		z[i] = eye[i] - (eye[i] + direction[i]);
	z = normalize(z);
	const auto x = normalize(cross(up, z)), y = cross(z, x);
	const auto proxy = RotationMatrixQuaternion({ x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, 0, 0, 0, 1 });
	const auto inverse = Conjugate(camera.rotation);
	LocalPose root;
	Vector offset;
	for (unsigned i = 0; i < 3; ++i)
		offset[i] = eye[i] - camera.position[i];
	root.position = RotateVector(offset, inverse);
	root.rotation = NormalizeQuaternion(QuaternionProduct(inverse, proxy));
	return Compose(root);
}
Matrix HeadRelativeModelRoot(const Vector& socket)
{
	const double c = std::cos(kPi), s = std::sin(kPi);
	LocalPose root;
	root.position = { socket[0], -socket[1], socket[2] };
	root.rotation = RotationMatrixQuaternion({ c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1 });
	return Compose(root);
}
}
