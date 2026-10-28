#pragma once
class CActor;
class CWeapon;
class player_hud;
namespace BodycamReference
{
class EngineCharacterHost;
struct CharacterMovementInput;
struct EngineCharacterView;
// Runs the Reference character of the focused actor once per frame: reads the
// engine's state, solves the frame and writes the camera and the HUD.
class EngineFrameAdapter
{
public:
	// True when the camera and the HUD are done for this frame; the caller must not update them again.
	static bool Update(CActor&, CWeapon*);

private:
	struct Frame;
	static bool Run(Frame&, bool hadWeaponProjection);
	static void Leave(CActor&, player_hud&);
	static bool FinishWithoutReference(Frame&);
	static bool BindWeapon(Frame&, EngineCharacterHost&);
	static void AdmitAction(Frame&, EngineCharacterHost&);
	static CharacterMovementInput ObserveMovement(CActor&, CWeapon*, EngineCharacterHost&);
	static void ApplyScriptCameraDeltas(CActor&, EngineCharacterHost&);
	static void PrepareCameraAndHud(Frame&, const EngineCharacterHost&, bool weaponPose, bool aiming);
	static void ObserveFrame(Frame&, EngineCharacterHost&, bool weaponPose, bool primarySightAimed, EngineCharacterView&);
	static void ObserveHands(Frame&, EngineCharacterView&);
	static void PublishHud(Frame&, const EngineCharacterHost&, const Fmatrix& world, bool weaponPose, float sceneFov);
	static void SyncLegs(CActor&, const EngineCharacterHost&);
};
}
