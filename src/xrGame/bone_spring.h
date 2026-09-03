#pragma once

#include "../xrEngine/bone.h"

class CActor;
class IKinematics;

// Secondary-motion ("jiggle") controller for a small set of bones.
//
// Each controlled bone gets a bctCustom callback that runs inside CKinematics::CLBone,
// after BuildBoneMatrix has produced the animated pose and before mRenderTransform is
// composed - so the displacement is picked up by the skinning automatically, and
// CKinematics::Bone_Calculate recurses into the children using the modified mTransform.
//
// The callbacks are installed on the ACTOR visual, not on the first-person body model.
// player_legs_controller::copy_bones_from_actor calls actor_K->CalculateBones(TRUE) and
// then copies the finished mTransform / mRenderTransform across, so this one
// implementation covers first person, third person and the shadow pass.
//
// The spring runs in MODEL space, so steady locomotion produces no displacement (the
// bone is not moving relative to the body, and a world-space spring would sit pinned at
// its clamp for as long as you ran). Whole-body motion - jumping, landing - is injected
// separately as a world-space acceleration term, see jiggle_world_gain.
class CBoneSpringController
{
public:
    enum
    {
        MAX_SPRING_BONES = 8
    };

    CBoneSpringController();

    // Installs bone callbacks on the actor's current visual, reading tuning from the
    // equipped outfit's section (falling back to [actor]). Safe to call repeatedly.
    // Does nothing if the visual has none of the configured bones, so outfits without
    // jiggle bones cost nothing at all.
    void install(CActor* actor);

    // Uninstalls the callbacks. Requires the visual to still be alive.
    void remove();

    // Drops all state WITHOUT touching the visual. Call this before the visual is
    // destroyed or replaced - CActor::OnChangeVisual and CActor::net_Destroy.
    void detach();

private:
    struct bone_spring
    {
        CBoneSpringController* owner;
        u16 bone_id;

        Fvector sim_pos;  // simulated (lagging) position, model space
        Fvector velocity; // model space
        Fvector applied;  // displacement applied this frame, cached for repeat calls
        u32 last_frame;
        bool primed;
    };

    static void _BCL bone_callback(CBoneInstance* bi);

    void integrate(bone_spring& b, CBoneInstance* bi);
    void apply(bone_spring& b, CBoneInstance* bi);
    void update_external_accel();
    void read_config(CActor* actor);

    CActor* m_actor;
    IKinematics* m_visual;

    bone_spring m_bones[MAX_SPRING_BONES];
    u16 m_bone_count;

    // whole-body motion tracking, refreshed at most once per frame
    Fvector m_prev_world_pos;
    Fvector m_prev_world_vel;
    Fvector m_external_accel; // already rotated into model space
    u32 m_accel_frame;
    bool m_world_primed;
};
