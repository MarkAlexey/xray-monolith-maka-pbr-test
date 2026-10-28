#pragma once

// The player in the scope glass reflection (gamedata/shaders/r3/scope_glass_head.h): the player's body
// model through the VSLR mirror image where it is current, and a low-detail figure (head, neck, shoulders
// and chest as spheres around the eye) that hides glints behind the player and stands in for the body
// otherwise. Settings: the r__svp_lens_shooter* and r__svp_lens_head* console variables (svp_console.cpp).
// Whether the glass shows the player: aiming, or at the hip when that is on.
bool scope_glass_shooter_shown(bool aiming);
// Binds scope_glass_body, scope_glass_head_params and the mirror camera for the current camera.
void scope_glass_head_bind(bool aiming);
// The point the shooter's body is placed at: the eye moved by r__svp_lens_shooter_x/y/z along the view's
// right, up and forward.
Fvector scope_glass_shooter_eye(const Fvector& eye, const Fvector& right, const Fvector& up, const Fvector& forward);
