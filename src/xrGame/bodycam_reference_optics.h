#pragma once
#include <cstdint>
#include <optional>
class CWeapon;
namespace BodycamReference
{
// The scope eye under each Classic lag model. The deadzone lag keeps the eye
// tracker; the follow lag shows the current eye, as Reference does. Reference
// mode sets neither.
struct ScopeEyeModel
{
	bool tracked = false;
	bool current = false;
};
inline ScopeEyeModel SelectScopeEyeModel(bool classic, bool followLag)
{
	return { classic && !followLag, classic && followLag };
}
// Publishes the scope effects frame for the weapon's optic, or clears it. Runs
// in both motion modes, once the weapon's final HUD transform is known. A
// profile that fails to load records the mode request in failedRequest and is
// not loaded again until the next request or the next HUD; player_hud::OnFrame
// clears failedRequest on any frame that sees the scope effects turned off.
void PublishOptics(CWeapon&, std::optional<std::uint64_t>& failedRequest);
}
