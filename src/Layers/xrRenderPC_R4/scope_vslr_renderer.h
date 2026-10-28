#pragma once

class light_Package;

// Scope reflections: a low-resolution cube capture of the static scene around the camera (VSLR),
// traced by the scope glass and by a world-space inspection sphere.
// r__scope_vslr: 1 the scope glass reflects the capture and the player, 0 the weather cubes only.
// r__scope_vslr_debug: 0 off, 1 sphere, 2 sphere and once-per-second [VSLR-COST] and [VSLR-MIRROR] log
// lines, 3 to 5 the capture lit by the sun's shadow alone, the ambient alone and the sun alone.
// r__scope_vslr_smooth: 1 also captures while the camera moves (see r__scope_vslr_move_interval) and
// consumers blend between the last two captures, 0 captures every 250 ms and shows each at once.
extern int ps_scope_vslr;
extern int ps_scope_vslr_debug;
extern int ps_scope_vslr_smooth;
// r__scope_vslr_move_interval: least time in ms between captures started by movement (0: back to back,
// one capture every three frames).
extern int ps_scope_vslr_move_interval;
// r__scope_vslr_cost 1 logs [VSLR-COST] once a second, as r__scope_vslr_debug 2 does, without its
// debug sphere.
extern int ps_scope_vslr_cost;
// r__scope_vslr_indirect: strength of the light the capture bounces onto its surfaces.
extern float ps_scope_vslr_indirect;

// Once per frame after the combine: captures faces while requested, publishes complete captures,
// draws the player's mirror image while the glass shows the player, and the debug sphere.
void scope_vslr_render(CRenderTarget& target);
// A consumer traces the capture this frame; capturing stops ten seconds after the last request. The
// capture's resources are made on a level's first frame and kept for the level.
void scope_vslr_request();
// Whether captures are running (requested within the last ten seconds and switched on).
bool scope_vslr_active();
// The published capture's textures and samplers (vslr_trace.h) for a consumer's pass.
void scope_vslr_bind(CBlender_Compile& C);
// With [VSLR-COST] logging on, the GPU time between these calls joins its line.
void scope_vslr_consumer_begin();
void scope_vslr_consumer_end();
// Centre and radial limit (w) of the published capture; zero when there is none.
Fvector4 scope_vslr_published();
// Weight of the published capture against the previous one (vslr_lobe_faded), 1 once blended in.
float scope_vslr_fade();
// The viewer's mirror image (vslr_mirror, bound by scope_vslr_bind) for the shooter in the glass: the
// mirror camera for the given eye and view axes, forward.w the tangent of its half field of view. False
// while none is current (the shooter figure off, or no viewer body).
// head: the body's head (at the eye), w the depth of its face in front of it.
bool scope_vslr_mirror_view(const Fvector& eye, const Fvector& view_forward, const Fvector& view_up, Fvector4& origin,
    Fvector4& right, Fvector4& up, Fvector4& forward, Fvector4& head);
// After the frame's lights are gathered: keeps the strongest lamps that reach the capture radius.
void scope_vslr_scene_lights(const light_Package& normal, const light_Package& pending);
// After the main view's widest sun cascade is accumulated: keeps its shadow map and bias for the
// capture's sun shadow while captures run.
void scope_vslr_sun_cascade(float bias);
// Releases captures and the retained sector; called on target destruction and level unload.
void scope_vslr_destroy();
