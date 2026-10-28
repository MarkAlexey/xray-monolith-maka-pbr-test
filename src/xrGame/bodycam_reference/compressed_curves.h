#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
namespace BodycamReference
{
struct NamedCurveEntry
{
	std::string name;
	float value = 0;
	std::uint8_t flags = 0;
};
using NamedCurves = std::vector<NamedCurveEntry>;
// The entry called name; appended with value 0 when the list has none.
NamedCurveEntry& FindOrAddCurve(NamedCurves& curves, const std::string& name);
struct CompressedCurveKey
{
	float time, value;
	std::uint8_t mode;
	float arrive = 0, leave = 0;
};
class CompressedLocomotionCurves
{
public:
	CompressedLocomotionCurves(const std::vector<std::uint8_t>& bytes, const std::vector<std::string>& names);
	NamedCurves Sample(float time, const std::set<std::string>& admittedNames) const;

private:
	struct Curve
	{
		std::string name;
		float constant = 0;
		std::vector<CompressedCurveKey> keys;
		float Sample(float) const;
	};
	std::vector<Curve> curves_;
};
struct WeightedNamedCurves
{
	float weight;
	NamedCurves curves;
};
NamedCurves BlendWeightedCurves(const std::vector<WeightedNamedCurves>& samples);
}
