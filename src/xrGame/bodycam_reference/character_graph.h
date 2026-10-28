#pragma once
#include "character_frame.h"

namespace BodycamReference
{
struct CharacterRandomSource
{
	std::function<unsigned()> draw15;
	std::function<double()> unit;
};
// The per-actor objects, declared in dependency order. The asset banks are held by
// reference and must outlive the graph.
class CharacterGraph
{
private:
	// Declared first: the constructors of the members below capture it.
	CharacterRandomSource randomSource_;

public:
	CharacterGraph(const RecoilSetup&, const std::vector<double>& regripDurations, const MovementCurveSampler&, const PoseAssetBank&,
		const LocomotionAssetBank&, const RifleAssetBank&, const Matrix& magnifiedAim, const Matrix& cantedAim, CharacterRandomSource,
		Rotation initialControl, double horizontalFov, WalkingStressAdapter stress = {});
	CharacterGraph(const CharacterGraph&) = delete;
	CharacterGraph& operator=(const CharacterGraph&) = delete;
	CharacterGraph(CharacterGraph&&) = delete;
	CharacterGraph& operator=(CharacterGraph&&) = delete;

	RecoilRig recoil;
	RecoilRandom random;
	RegripMontages regrip;
	MotionFrame motion;
	GroundedMovement movement;
	PrimaryFrame primary;
	AimPointBlend aim;
	MovementCameraVibration vibration;
	MouseShakeLayer mouse;
	CameraEffects effects;
	CharacterFrame character;
};
}
