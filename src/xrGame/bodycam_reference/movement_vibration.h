#pragma once
#include "transform.h"
#include "control.h"
#include <functional>
#include <optional>
namespace BodycamReference
{
float Perlin1D(float);
struct CameraPov
{
	Vector location{};
	Rotation rotation;
	double fov = 0;
};
struct MovementVibrationInput
{
	double speed = 0, yawRate = 0, pitchRate = 0, tremor = 0;
};
struct VibrationRaw
{
	Rotation rotation;
	float envelope = 0, scale = 0;
};
struct VibrationState
{
	std::array<float, 7> phase{}; // z, y, x, roll, yaw, pitch, fov: RNG order.
	float scale = .5f, elapsed = 0, inTime = 0;
	bool blendingIn = true;
	double intensity = 1, target = 0;
	std::optional<VibrationRaw> raw;
};
double MovementVibrationTarget(const MovementVibrationInput&, double intensity);
CameraPov ApplyMovementVibration(const CameraPov&, const VibrationRaw&, double scale);
CameraPov ApplyCameraShake(const CameraPov&, const Vector& location, const Rotation&, double scale);
class MovementCameraVibration
{
public:
	explicit MovementCameraVibration(const std::function<unsigned()>& draw15);
	void ComponentTick(double dt, const MovementVibrationInput&);
	void PublishIntensity(bool blockingHit, double distanceCm);
	CameraPov Apply(const CameraPov&, double dt);
	const VibrationState& State() const { return state_; }

private:
	VibrationState state_;
};
}
