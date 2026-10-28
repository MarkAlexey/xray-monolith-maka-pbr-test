// The weapon's side of Reference actions: fire stops when an action clip
// starts, and aim input waits for the clip to finish.
#include "stdafx.h"
#include "Weapon.h"
#include "Actor.h"
#include "Actor_Flags.h"
#include "Inventory.h"
#include "Level.h"
#include "UIGameCustom.h"
#include "player_hud.h"
#include "xr_level_controller.h"
#include "../xrEngine/xr_input.h"

extern bool g_bDisableAllInput;
extern bool g_block_all_except_movement;

void CWeapon::CancelReferenceActionFire()
{
	CWeapon::FireEnd();
}

void CWeapon::CancelReferenceActionAim()
{
	m_referenceActionAimGate.Cancel(IsZoomed());
	OnZoomOut();
}

bool CWeapon::RejectReferenceActionAimStart()
{
	const bool reference = g_player_hud && g_player_hud->ReferenceOwnsMotion(this);
	const auto action = reference ? g_player_hud->ReferenceActionStatus() : HudActionState{};
	return m_referenceActionAimGate.RejectStart(reference, action.playing && (!action.reload || IsZoomed() || !action.preservesAim),
		action.preservesAim, psActorFlags.test(AF_AIM_TOGGLE));
}

// True when Reference has taken the zoom command.
bool CWeapon::BodycamReferenceZoom(u32 flags)
{
	// During a reload that keeps the aim, aim input changes the zoom only, so
	// a held-ADS retry can neither switch to eAimStart nor replace the reload.
	const bool reference = IsZoomEnabled() && g_player_hud && g_player_hud->ReferenceOwnsMotion(this);
	const auto action = reference ? g_player_hud->ReferenceActionStatus() : HudActionState{};
	if (action.reload && action.preservesAim)
	{
		if (flags & CMD_START)
		{
			if (!IsZoomed())
				OnZoomIn();
		}
		else
		{
			m_referenceActionAimGate.Release();
			if (IsZoomed())
				OnZoomOut();
		}
		return true;
	}
	if (!(flags & CMD_START))
	{
		m_referenceActionAimGate.Release();
		return false;
	}
	return RejectReferenceActionAimStart();
}

// A held key does not repeat the engine's zoom press, so the aim a Reference
// action deferred is resumed here, through the normal weapon path.
void CWeapon::BodycamResumeReferenceAim()
{
	if (!m_referenceActionAimGate.ResumePending())
		return;
	CActor* actor = smart_cast<CActor*>(H_Parent());
	const bool eligible = actor && actor == Level().CurrentEntity() && actor->g_Alive() && actor->inventory().ActiveItem() == this &&
		!actor->Holder() && !actor->IsTalking() && !actor->input_external_handler_installed() && !g_bDisableAllInput &&
		!g_block_all_except_movement && !Device.Paused() && !Device.imgui_shown() && CurrentGameUI() &&
		!CurrentGameUI()->TopInputReceiver() && IsZoomEnabled() && g_player_hud && g_player_hud->ReferenceOwnsMotion(this);
	const int primary = get_action_dik(kWPN_ZOOM, 0);
	const int secondary = get_action_dik(kWPN_ZOOM, 1);
	const bool keyDown = (primary && pInput->iGetAsyncKeyState(primary)) || (secondary && pInput->iGetAsyncKeyState(secondary));
	if (m_referenceActionAimGate.ConsumeHeldResume(eligible, keyDown, eligible && g_player_hud->ReferenceSemanticActionPlaying(),
			IsPending(), IsZoomed(), psActorFlags.test(AF_AIM_TOGGLE)))
		Action(kWPN_ZOOM, CMD_START);
}
