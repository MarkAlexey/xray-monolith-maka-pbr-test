// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "movement_vibration.h"
#include "constants.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
constexpr unsigned permutation[] = { 63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99,
	222, 2, 41, 15, 197, 93, 169, 90, 228, 43, 221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219, 78, 224, 13, 193, 88, 134,
	211, 7, 112, 176, 19, 106, 83, 75, 217, 85, 0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238, 172, 82, 110, 4, 227, 77, 243,
	46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194, 57, 48, 247, 233, 51, 105, 5, 23, 42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108,
	241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21, 244, 60, 223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160, 124, 156,
	79, 186, 135, 127, 14, 121, 22, 65, 54, 153, 91, 213, 174, 24, 252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147,
	25, 67, 36, 92, 236, 71, 166, 1, 187, 100, 130, 143, 237, 178, 158, 104, 184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3,
	248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74, 150, 218, 29, 133, 8, 44, 123, 28, 146, 89, 101, 154,
	220, 126, 155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164, 167, 215, 246, 144, 107, 235, 63 };
constexpr float gradients[] = { -1, -.875f, -.75f, -.625f, -.5f, -.375f, -.25f, -.125f, .125f, .25f, .375f, .5f, .625f, .75f, .875f, 1 };
double Atan2(double y, double x)
{
	return y == 0 && x == 0 ? 0 : std::atan2(y, x);
}
Matrix RotationMatrix(Rotation r)
{
	constexpr double degrees = double(kRadiansPerDegreeF);
	const double sp = std::sin(r.pitch * degrees), cp = std::cos(r.pitch * degrees), sy = std::sin(r.yaw * degrees),
				 cy = std::cos(r.yaw * degrees), sr = std::sin(r.roll * degrees), cr = std::cos(r.roll * degrees);
	return { cy * cp, cp * sy, sp, 0, ((sr * sp) * cy) - (cr * sy), ((sr * sp) * sy) + (cr * cy), -(cp * sr), 0,
		-(((cr * sp) * cy) + (sr * sy)), (cy * sr) - ((cr * sp) * sy), cr * cp, 0, 0, 0, 0, 1 };
}
Rotation MatrixRotator(const Matrix& m)
{
	constexpr double degrees = kDegreesPerRadian;
	const double yaw = Atan2(m[1], m[0]) * degrees, pitch = Atan2(m[2], std::sqrt((m[1] * m[1]) + (m[0] * m[0]))) * degrees;
	const auto s = RotationMatrix({ pitch, yaw, 0 });
	const double numerator = ((s[4] * m[8]) + (s[5] * m[9])) + (s[6] * m[10]),
				 denominator = ((s[5] * m[5]) + (s[4] * m[4])) + (s[6] * m[6]);
	return { pitch, yaw, Atan2(numerator, denominator) * degrees };
}
Matrix RowMultiply(const Matrix& a, const Matrix& b)
{
	Matrix out;
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 4; ++c)
			out[r * 4 + c] = (((a[r * 4] * b[c]) + (a[r * 4 + 1] * b[4 + c])) + (a[r * 4 + 2] * b[8 + c])) + (a[r * 4 + 3] * b[12 + c]);
	return out;
}
float NormalFloat(float a)
{
	a = std::fmod(a, 360.f);
	if (a < 0)
		a += 360.f;
	if (a > 180)
		a -= 360.f;
	return a;
}
float ScaledPitch(double base, double offset)
{
	const float b = float(WrapDegrees(base)), v = b + float(WrapDegrees(offset)), n = NormalFloat(v);
	return NormalFloat(n > 89.9f ? 89.9f : n < -89.9f ? -89.9f : v) - b;
}
}
float Perlin1D(float x)
{
	const double floor = std::floor(double(x));
	const float t = float(double(x) - floor);
	const unsigned index = std::isfinite(floor) && floor >= -2147483648. && floor < 2147483648. ? unsigned(int(floor)) & 255 : 0;
	const float t2 = t * t, a = t * gradients[permutation[index] & 15];
	float fade = t * 6;
	fade -= 15;
	fade *= t;
	fade += 10;
	fade *= t;
	fade *= t2;
	fade += fade;
	const float b = (t - 1) * gradients[permutation[index + 1] & 15];
	return ((b - a) * fade + a) + a;
}
double MovementVibrationTarget(const MovementVibrationInput& in, double intensity)
{
	for (double v : { in.speed, in.yawRate, in.pitchRate, in.tremor, intensity })
		if (!std::isfinite(v))
			throw std::invalid_argument("Vibration input is not finite");
	const double motion = in.speed < 150
		? MapRangeClamped(in.speed, 0, 150, 0, 1)
		: MapRangeClamped(in.speed, 20, 300, 0, 2) + MapRangeClamped(std::abs(in.yawRate + in.pitchRate), 1, 2, 0, 1);
	return (motion + MapRangeClamped(in.tremor, .5, 1, 0, 2)) * intensity;
}
CameraPov ApplyMovementVibration(const CameraPov& base, const VibrationRaw& raw, double shakeScale)
{
	const float scale = std::max(0.f, float(shakeScale));
	const Rotation rotation{ ScaledPitch(base.rotation.pitch, raw.rotation.pitch * scale), raw.rotation.yaw * scale,
		raw.rotation.roll * scale };
	auto out = base;
	out.rotation = MatrixRotator(RowMultiply(RotationMatrix(rotation), RotationMatrix(base.rotation)));
	return out;
}
CameraPov ApplyCameraShake(const CameraPov& base, const Vector& location, const Rotation& raw, double shakeScale)
{
	const float scale = std::max(0.f, float(shakeScale));
	const double b = float(WrapDegrees(base.rotation.pitch)), v = b + WrapDegrees(raw.pitch);
	const double n = WrapDegrees(v), limit = double(89.9f);
	const double pitch = WrapDegrees(n > limit ? limit : n < -limit ? -limit : v) - b;
	const Rotation rotation{ ScaledPitch(base.rotation.pitch, pitch * scale), raw.yaw * scale, raw.roll * scale };
	const auto matrix = RotationMatrix(base.rotation);
	auto out = base;
	const Vector loc{ location[0] * scale, location[1] * scale, location[2] * scale };
	for (unsigned i = 0; i < 3; ++i)
		out.location[i] = (((loc[1] * matrix[4 + i]) + (loc[0] * matrix[i])) + (loc[2] * matrix[8 + i]) + 0) + base.location[i];
	out.rotation = MatrixRotator(RowMultiply(RotationMatrix(rotation), matrix));
	out.fov = float(float(base.fov) + float(0.f * scale));
	return out;
}
MovementCameraVibration::MovementCameraVibration(const std::function<unsigned()>& draw15)
{
	for (auto& phase : state_.phase)
	{
		// Start phase draw * float(255 / 32767), at most 254. Every axis draws, used or not.
		phase = float(std::min(int(float(draw15()) * 0x1.fe03fcp-8f), 254));
	}
}
void MovementCameraVibration::ComponentTick(double dt, const MovementVibrationInput& in)
{
	state_.target = MovementVibrationTarget(in, state_.intensity);
	state_.scale = float(FInterpTo(state_.scale, state_.target, float(dt), 5));
}
void MovementCameraVibration::PublishIntensity(bool hit, double distance)
{
	state_.intensity = hit ? MapRangeClamped(float(distance), 350, 1500, 1, .3) : .3;
}
CameraPov MovementCameraVibration::Apply(const CameraPov& base, double dt)
{
	if (dt > 0)
	{
		std::array<float, 3> values;
		const std::array<unsigned, 3> indices = { 5, 4, 3 };
		const std::array<float, 3> amplitudes = { .3f, .25f, .2f }, frequencies = { 11, 9, 13 };
		for (unsigned i = 0; i < 3; ++i)
		{
			auto& phase = state_.phase[indices[i]];
			phase = float(dt) * frequencies[i] + phase;
			values[i] = Perlin1D(phase) * amplitudes[i];
		}
		state_.elapsed += float(dt);
		float envelope = 1;
		if (state_.blendingIn)
		{
			state_.inTime += float(dt);
			if (state_.inTime < .2f)
				envelope = state_.inTime / .2f;
			else
			{
				state_.inTime = .2f;
				state_.blendingIn = false;
			}
		}
		state_.raw = VibrationRaw{ { double(values[0]) * envelope, double(values[1]) * envelope, double(values[2]) * envelope }, envelope,
			state_.scale };
	}
	return state_.raw ? ApplyMovementVibration(base, *state_.raw, state_.scale) : base;
}
}
