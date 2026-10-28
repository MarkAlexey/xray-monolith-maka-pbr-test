// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_random.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
RecoilRandom::RecoilRandom(std::function<unsigned()> draw15) : draw15_(std::move(draw15)) {}
float RecoilRandom::Unit()
{
	const unsigned value = draw15_();
	if (value > kRandomDrawMax)
		throw std::invalid_argument("Random draw exceeds 32767");
	++draws_;
	return float(double(value) * float(1. / kRandomDrawMax));
}
double RecoilRandom::Range(double minimum, double maximum)
{
	return double(Unit()) * (maximum - minimum) + minimum;
}
double RecoilRandom::Signed(const std::array<double, 4>& v)
{
	const float unit = Unit();
	const int choice = std::min(static_cast<int>(float(double(unit) + unit)), 1);
	// Both candidate ranges draw, even though only one value is retained.
	const double zw = Range(v[2], v[3]);
	const double xy = Range(v[0], v[1]);
	return choice == 1 ? zw : xy;
}
std::array<double, 3> RecoilRandom::Location(const RecoilLocationProfile& p)
{
	double x = float(Range(p.x[0] * p.scale[0], p.x[1] * p.scale[1]));
	double z = float(Range(p.z[0] * p.scale[4], p.z[1] * p.scale[5]));
	const float maxX = float(p.maxX), maxZ = float(p.maxZ);
	const auto clamp = [](double v, float m)
	{
		const double a = v < m ? v : m;
		return a > -m ? a : -m;
	};
	if (maxX != 0)
		x = clamp(x, maxX);
	const double y = Range(p.y[0] * p.scale[2], p.y[1] * p.scale[3]);
	if (maxZ != 0)
		z = clamp(z, maxZ);
	return { x, y, z };
}
RecoilRandomTargets RecoilRandom::Targets(const RecoilRandomProfile& p, double previousRoll)
{
	double roll = float(Signed(p.roll));
	if (p.smoothRoll && roll * previousRoll > 0)
		roll = -roll;
	const double pitch = Range(p.pitch[0], p.pitch[1]);
	const double yaw = Signed(p.yaw);
	return { { roll, pitch, yaw }, Location(p.location) };
}
}
