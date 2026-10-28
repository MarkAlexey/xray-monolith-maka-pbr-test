#include "movement_asset.h"
#include "asset_reader.h"
namespace BodycamReference
{
MovementCurveSampler LoadMovementSampler(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRMOVE1", 1);
	const auto read = [&]()
	{
		std::vector<RecoilCurveKey> keys(r.Count(4096));
		for (auto& k : keys)
		{
			k.time = r.F32();
			k.value = r.F32();
			k.arrive = r.F32();
			k.leave = r.F32();
		}
		return RecoilCurve(std::move(keys));
	};
	std::array<RecoilCurve, 3> curves{ read(), read(), read() };
	if (!r.Done())
		throw std::invalid_argument("Trailing movement package data");
	return [curves = std::move(curves)](unsigned axis, double time) { return double(curves.at(axis).Sample(float(time))); };
}
}
