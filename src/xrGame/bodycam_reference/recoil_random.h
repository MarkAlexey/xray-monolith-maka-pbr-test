#pragma once
#include <array>
#include <cstdint>
#include <functional>

namespace BodycamReference
{
struct RecoilLocationProfile
{
	std::array<double, 2> x{}, y{}, z{};
	std::array<double, 6> scale{ 1, 1, 1, 1, 1, 1 };
	double maxX = 0, maxZ = 0;
};
struct RecoilRandomProfile
{
	std::array<double, 2> pitch{};
	std::array<double, 4> yaw{}, roll{};
	RecoilLocationProfile location;
	bool smoothRoll = false;
};
struct RecoilRandomTargets
{
	std::array<double, 3> rotation{}; // roll, pitch, yaw
	std::array<double, 3> location{}; // X, Y, Z
};
// The largest value a 15-bit random draw returns.
constexpr unsigned kRandomDrawMax = 32767;
// Draws from the host's random stream.
class RecoilRandom
{
public:
	explicit RecoilRandom(std::function<unsigned()> draw15);
	float Unit();
	double Range(double minimum, double maximum);
	double Signed(const std::array<double, 4>& interval);
	std::array<double, 3> Location(const RecoilLocationProfile&);
	RecoilRandomTargets Targets(const RecoilRandomProfile&, double previousRoll);
	std::uint64_t Draws() const { return draws_; }

private:
	std::function<unsigned()> draw15_;
	std::uint64_t draws_ = 0;
};
}
