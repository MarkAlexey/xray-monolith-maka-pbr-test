#pragma once
class CPhysicsShell;
class CCameraBase;
class IPhysicsShellHolder;
extern XRPHYSICS_API CPhysicsShell* actor_camera_shell;
#ifdef DEBUG
extern XRPHYSICS_API BOOL dbg_draw_camera_collision;
extern XRPHYSICS_API float	camera_collision_character_skin_depth ;
extern XRPHYSICS_API float	camera_collision_character_shift_z ;
#endif
extern XRPHYSICS_API BOOL g_clamp_actor_camera_collision;
XRPHYSICS_API bool test_camera_box(const Fvector& box_size, const Fmatrix& xform, IPhysicsShellHolder* l_actor);
XRPHYSICS_API void collide_camera(CCameraBase& camera, float _viewport_near, IPhysicsShellHolder* l_actor);

// Reference-only safe-anchor sweep; native overlap/material/contact behavior.
XRPHYSICS_API void collide_camera_swept(CCameraBase& camera, float viewport_near, IPhysicsShellHolder* actor,
    const Fvector& start, const Fvector& localAnchor, Fvector* releaseOffset = nullptr,
    float delta = 0.f, bool diagnostics = false);
