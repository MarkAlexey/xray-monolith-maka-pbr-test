#pragma once
namespace BodycamReference
{
struct ShotOwnership
{
	bool referenceCamera = false;
	bool primaryItem = false;
	bool firearm = false;
	bool engineTool = false;
};
inline bool OwnsEngineShot(const ShotOwnership& owner)
{
	// Firearms without a rig binding get the shot too. Tools can derive from CWeapon; their
	// attack animations are not shots.
	return owner.referenceCamera && owner.primaryItem && owner.firearm && !owner.engineTool;
}
}
