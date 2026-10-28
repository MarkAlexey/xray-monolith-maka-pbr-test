// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "camera.h"
#include "pose.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
namespace
{
// Rates are look input per second divided by kLookRateScale. Rolls are degrees. Speeds are interpolation rates per second.
// A yaw rate of kTurnRollRate rolls the view by kTurnRoll, full strafe input by kStrafeRoll.
constexpr double kTurnRollRate = 3, kTurnRoll = 8, kStrafeRoll = 5;
// Above this speed the lean roll is added to the turn roll.
constexpr double kLeanRollSpeedCm = 250;
// While aiming or leaning the roll target is the lean roll scaled by speed.
constexpr double kLeanScaleSlowCm = 120, kLeanScaleFastCm = 200, kLeanScaleSlow = 2, kLeanScaleFast = 1.5;
constexpr double kRollSpeed = 8;
// The lean target follows the yaw rate when aiming and the summed rates otherwise.
constexpr double kHipLeanRate = 2, kHipLean = .4;
constexpr double kLeanTargetSlowRate = .5, kLeanTargetFastRate = 2, kLeanTargetSlowSpeed = 3, kLeanTargetFastSpeed = 10;
constexpr double kLeanAlphaSpeed = 12;
constexpr double kMaxLeanRoll = 15;
// The arms lag speed returns to kArmsLagSpeed at a rate that grows with the pitch rate in rotation mode 2.
constexpr double kArmsLagSpeed = 20, kArmsLagSlowRate = .5, kArmsLagFastRate = 15, kArmsLagPitchRate = .3;
// The summed absolute rates over which the camera lag speed rises (CameraMotionTuning).
constexpr double kCameraLagSlowRate = 1, kCameraLagFastRate = 2;
constexpr float kMaxLagStepSeconds = float(.033333);
// Components and angles within this tolerance count as equal.
constexpr float kRotationTolerance = float(.0001);
// |x z - w y| above this is a pitch of +-90 degrees.
constexpr double kPoleTest = .4999995;
// Quaternions closer than this (by dot product) blend linearly instead of spherically.
constexpr double kSlerpLinearDot = .9999;
double Clamp(double x)
{
	return std::clamp(x, 0.0, 1.0);
}
}
Rotation InterpolateControlRotation(Rotation current, Rotation target, double delta, double speed)
{
	const float dt = float(delta), rate = float(speed);
	if (dt == 0 || (current.pitch == target.pitch && current.yaw == target.yaw && current.roll == target.roll))
		return current;
	if (rate <= 0)
		return target;
	const Rotation d{ WrapDegrees(target.pitch - current.pitch), WrapDegrees(target.yaw - current.yaw),
		WrapDegrees(target.roll - current.roll) };
	if (std::abs(WrapDegrees(d.pitch)) <= kRotationTolerance && std::abs(WrapDegrees(d.yaw)) <= kRotationTolerance &&
		std::abs(WrapDegrees(d.roll)) <= kRotationTolerance)
		return target;
	const double a = Clamp(float(double(dt) * rate));
	return { WrapDegrees(current.pitch + d.pitch * a), WrapDegrees(current.yaw + d.yaw * a), WrapDegrees(current.roll + d.roll * a) };
}
namespace
{
Quaternion ToQuaternion(Rotation r)
{
	const double h = kPi / 360, p = r.pitch * h, y = r.yaw * h, l = r.roll * h;
	const double cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y), cr = std::cos(l), sr = std::sin(l);
	return { cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy };
}
Rotation ToRotation(const Quaternion& q)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3], test = x * z - w * y;
	const double yaw = std::atan2(2 * (x * y + w * z), 1 - 2 * (y * y + z * z)) * kDegreesPerRadian;
	if (test < -kPoleTest)
		return { -90, yaw, WrapDegrees(-yaw - 2 * std::atan2(x, w) * kDegreesPerRadian) };
	if (test > kPoleTest)
		return { 90, yaw, WrapDegrees(yaw - 2 * std::atan2(x, w) * kDegreesPerRadian) };
	return { std::asin(2 * test) * kDegreesPerRadian, yaw, std::atan2(-2 * (x * w + y * z), 1 - 2 * (x * x + y * y)) * kDegreesPerRadian };
}
Quaternion InterpQuaternion(const Quaternion& current, const Quaternion& target, double dt, double speed)
{
	bool same = true, opposite = true;
	for (int i = 0; i < 4; ++i)
	{
		same &= std::abs(current[i] - target[i]) <= kRotationTolerance;
		opposite &= std::abs(current[i] + target[i]) <= kRotationTolerance;
	}
	if (float(speed) <= 0 || same || opposite)
		return target;
	const double alpha = Clamp(float(double(float(dt)) * float(speed)));
	const double dot = (current[3] * target[3] + current[2] * target[2]) + (current[1] * target[1] + current[0] * target[0]);
	const double absolute = std::abs(dot);
	double a = 1 - alpha, b = dot < 0 ? -alpha : alpha;
	if (absolute < kSlerpLinearDot)
	{
		const double theta = std::acos(absolute), inverse = 1 / std::sin(theta);
		a = std::sin(theta * a) * inverse;
		b = std::sin(theta * b) * inverse;
	}
	Quaternion q;
	for (int i = 0; i < 4; ++i)
		q[i] = current[i] * a + target[i] * b;
	const double squared = (q[0] * q[0] + q[2] * q[2]) + (q[1] * q[1] + q[3] * q[3]);
	if (squared < float(1e-8))
		return { 0, 0, 0, 1 };
	const double inverse = 1 / std::sqrt(squared);
	for (auto& value : q)
		value *= inverse;
	return q;
}
Rotation Lag(Rotation current, Rotation target, double dt, double speed)
{
	return ToRotation(InterpQuaternion(ToQuaternion(current), ToQuaternion(target), std::min(float(dt), kMaxLagStepSeconds), speed));
}
}
const CameraState& CameraMotion::Update(const CameraInput& input)
{
	auto& s = state_;
	const double dt = input.worldDelta, inverse = dt == 0 ? 0 : 1 / dt;
	const double yawRate = (input.lookX * inverse) / kLookRateScale, pitchRate = (input.lookY * inverse) / kLookRateScale,
				 priorLeanRoll = s.currentLeanRoll;
	const Rotation target{ float(double(float(input.control.pitch))), float(double(float(input.control.yaw))),
		float(double(float(input.control.roll)) + float(priorLeanRoll)) };
	s.smoothControl = InterpolateControlRotation(s.smoothControl, target, dt, tuning_.controlSmoothSpeed);
	const double normalRoll = MapRangeClamped(yawRate, -kTurnRollRate, kTurnRollRate, kTurnRoll, -kTurnRoll) +
		MapRangeClamped(input.moveX, -1, 1, -kStrafeRoll, kStrafeRoll) + (input.speedCm > kLeanRollSpeedCm ? priorLeanRoll : 0);
	const double leanScale = MapRangeClamped(input.speedCm, kLeanScaleSlowCm, kLeanScaleFastCm, kLeanScaleSlow, kLeanScaleFast);
	const double rollTarget = input.aiming || input.leaningPressed ? priorLeanRoll * leanScale : normalRoll;
	s.roll = float(FInterpTo(s.roll, rollTarget, dt, kRollSpeed));
	const double sum = yawRate + pitchRate;
	s.leanTarget = FInterpTo(s.leanTarget,
		input.aiming ? MapRangeClamped(yawRate, -1, 1, 1, -1) : MapRangeClamped(sum, -kHipLeanRate, kHipLeanRate, kHipLean, -kHipLean), dt,
		MapRangeClamped(sum, kLeanTargetSlowRate, kLeanTargetFastRate, kLeanTargetSlowSpeed, kLeanTargetFastSpeed));
	s.leanAlpha = FInterpTo(s.leanAlpha, s.leanTarget, dt, kLeanAlphaSpeed);
	const double armsAdjust = input.rotationMode == 2
		? kArmsLagSlowRate + (kArmsLagFastRate - kArmsLagSlowRate) * (std::abs(pitchRate) / kArmsLagPitchRate)
		: kArmsLagSlowRate;
	s.armsLagSpeed = float(FInterpTo(s.armsLagSpeed, kArmsLagSpeed, dt, armsAdjust));
	s.lagSpeed = float(FInterpTo(s.lagSpeed,
		MapRangeClamped(std::abs(pitchRate) + std::abs(yawRate), kCameraLagSlowRate, kCameraLagFastRate, tuning_.cameraLagSlowSpeed,
			tuning_.cameraLagFastSpeed),
		dt, tuning_.cameraLagRate));
	// The arms use the endpoint of the previous frame; the displayed camera uses this frame's.
	s.finalYaw = s.armsEndpoint.yaw * kPi / 180;
	s.finalPitch = s.armsEndpoint.pitch * kPi / 180;
	s.finalRoll = s.armsEndpoint.roll;
	s.armsEndpoint = Lag(s.armsEndpoint, s.smoothControl, dt, s.armsLagSpeed);
	s.cameraEndpoint = Lag(s.cameraEndpoint, { float(s.smoothControl.pitch), float(s.smoothControl.yaw), s.roll }, dt, s.lagSpeed);
	s.yaw = s.cameraEndpoint.yaw * kPi / 180;
	s.pitch = s.cameraEndpoint.pitch * kPi / 180;
	s.displayRoll = s.cameraEndpoint.roll;
	if (s.leanAlpha != 0)
		s.currentLeanRoll = kMaxLeanRoll * s.leanAlpha;
	s.yawRate = yawRate;
	s.pitchRate = pitchRate;
	return s;
}
}
