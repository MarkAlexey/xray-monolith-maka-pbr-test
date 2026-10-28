#pragma once
namespace BodycamReference
{
// The settled ADS calibration follows the weapon mode, not the zoom state, so it stays the same
// during the ADS transition.
inline const char* AdsCalibrationAlias(bool launcherAttached, bool grenadeMode)
{
	return launcherAttached ? (grenadeMode ? "anm_idle_g_aim" : "anm_idle_w_gl_aim") : "anm_idle_aim";
}
// Index into the weapon's HUD offset table: 1 aim, 2 grenade-launcher aim, 3 alternative aim
// (CWeapon::ResolveHudOffsetTarget).
inline unsigned AdsCalibrationOffset(bool grenadeMode, unsigned zoomType)
{
	return grenadeMode ? 2u : (zoomType ? 3u : 1u);
}
}
