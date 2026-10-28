#pragma once

class player_hud;
struct lua_State;

namespace FirstPerson
{
// The frame's final camera and HUD transforms, read by scripts through the
// first_person Lua module.
// Called once the actor's camera is applied to the device.
void PublishCamera();
// Called once the HUD transforms of the frame are final.
void PublishHud(player_hud& hud);
// Called when the level is destroyed. Poses carry a generation that changes here.
void Reset();

void script_register(lua_State* state);
} // namespace FirstPerson
