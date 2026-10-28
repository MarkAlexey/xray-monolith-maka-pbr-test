#pragma once
#include "transform.h"
#include <cstdint>
#include <vector>
namespace BodycamReference
{
struct EquipmentTransforms
{
	Matrix magnifiedAim, cantedAim, optic, ocular, magnifiedProjection, cantedProjection;
};
EquipmentTransforms LoadEquipmentTransforms(const std::vector<std::uint8_t>&);
void ValidateEquipmentTransforms(const EquipmentTransforms&);
}
