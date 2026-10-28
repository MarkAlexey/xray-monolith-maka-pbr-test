#pragma once
#include <array>
#include <cstdint>
namespace BodycamReference
{
// Control degrees per unit of look input.
constexpr double kControlDegreesPerLook = 2.5;
struct Rotation
{
	double pitch = 0, yaw = 0, roll = 0;
};
struct MouseFrame
{
	std::array<double, 2> raw{};
	std::array<std::uint8_t, 2> samples{};
};
class MouseAccumulator
{
public:
	void Add(double x, double y);
	MouseFrame Consume();

private:
	MouseFrame frame_;
};
std::array<double, 2> ModifyRawLook(double x, double y, bool aiming);
Rotation ControllerDeltas(double lookX, double lookY);
Rotation ApplyControlDeltas(Rotation current, const Rotation& delta);
Rotation ApplyRawLook(Rotation current, double x, double y, bool aiming);
float AdvanceAds(float progress, bool aiming, double dt);
float AdsCurve(float progress);
}
