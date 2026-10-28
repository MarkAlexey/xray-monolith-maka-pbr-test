#include "equipment_asset.h"
#include "asset_reader.h"
#include <cmath>
namespace BodycamReference
{
void ValidateEquipmentTransforms(const EquipmentTransforms& t)
{
	for (const auto* matrix : { &t.magnifiedAim, &t.cantedAim, &t.optic, &t.ocular, &t.magnifiedProjection, &t.cantedProjection })
	{
		for (double value : *matrix)
			if (!std::isfinite(value))
				throw std::invalid_argument("Nonfinite equipment transform");
		if ((*matrix)[3] != 0 || (*matrix)[7] != 0 || (*matrix)[11] != 0 || (*matrix)[15] != 1)
			throw std::invalid_argument("Equipment transform is not affine");
		(void)InverseAffine(*matrix); // Throws for a singular transform.
	}
}
EquipmentTransforms LoadEquipmentTransforms(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCREQPT1", 1);
	EquipmentTransforms t;
	for (auto* matrix : { &t.magnifiedAim, &t.cantedAim, &t.optic, &t.ocular, &t.magnifiedProjection, &t.cantedProjection })
		for (auto& value : *matrix)
			value = r.F64();
	if (!r.Done())
		throw std::invalid_argument("Trailing equipment package data");
	ValidateEquipmentTransforms(t);
	return t;
}
}
