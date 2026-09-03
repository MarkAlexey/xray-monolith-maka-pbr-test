#include "stdafx.h"
#include "bone_spring.h"
#include "actor.h"
#include "CustomOutfit.h"
#include "../Include/xrRender/Kinematics.h"

// ---------------------------------------------------------------------------
// Tuning. These hold the ACTIVE values: install() overwrites them from the equipped
// outfit's section (or [actor]) every time the visual changes, and they are also
// registered as console commands so the look can be dialled in live. Workflow is
// therefore: tweak in the console until it looks right, then write the final numbers
// into the outfit's ltx section - config wins on the next re-equip.
// ---------------------------------------------------------------------------
BOOL jiggle_enabled = TRUE;
float jiggle_stiffness = 140.0f;    // spring constant; higher = snappier, tighter
float jiggle_damping = 11.0f;       // velocity damping; lower = more overshoot
float jiggle_max_offset = 0.035f;   // metres; hard clamp on displacement
float jiggle_translate_gain = 1.0f; // fraction of the lag applied as translation
float jiggle_rotate_gain = 1.6f;    // how much of the lag becomes a swing rotation
float jiggle_bone_length = 0.075f;  // virtual bone length used to convert lag -> angle
float jiggle_world_gain = 0.9f;     // jump / landing response
int jiggle_axis = 2;                // bone-local axis treated as "length": 0=i 1=j 2=k

static const char* JIGGLE_DEFAULT_BONES = "bip01_l_boob, bip01_r_boob";

// Integration is clamped and sub-stepped so that a frame hitch or a very stiff spring
// cannot make the simulation blow up.
static const float JIGGLE_MAX_DT = 0.05f;
static const float JIGGLE_SUBSTEP = 1.0f / 120.0f;
static const int JIGGLE_MAX_SUBSTEPS = 8;
// Movement larger than this in one frame counts as a teleport rather than motion, and
// the spring snaps instead of trying to catch up. Covers level change, ChangeVisual and
// the animation-to-ragdoll snap in imotion_position.
static const float JIGGLE_SNAP_DIST = 0.5f;
static const float JIGGLE_MAX_EXTERNAL_ACCEL = 60.0f;

// ---------------------------------------------------------------------------

CBoneSpringController::CBoneSpringController()
    : m_actor(nullptr), m_visual(nullptr), m_bone_count(0), m_accel_frame(0), m_world_primed(false)
{
    ZeroMemory(m_bones, sizeof(m_bones));
    m_prev_world_pos.set(0.0f, 0.0f, 0.0f);
    m_prev_world_vel.set(0.0f, 0.0f, 0.0f);
    m_external_accel.set(0.0f, 0.0f, 0.0f);
}

void CBoneSpringController::read_config(CActor* actor)
{
    // Prefer the equipped outfit's section so outfits can be tuned individually, and
    // fall back to [actor]. On undress GetOutfit() may still return the outfit here;
    // that is harmless, because an outfit's tuning only takes effect on a visual that
    // actually has the bones.
    shared_str sect;
    CCustomOutfit* outfit = actor ? actor->GetOutfit() : nullptr;
    if (outfit && pSettings->section_exist(outfit->cNameSect()))
        sect = outfit->cNameSect();

    struct
    {
        LPCSTR key;
        float* dst;
    } const fkeys[] = {
        {"jiggle_stiffness", &jiggle_stiffness},
        {"jiggle_damping", &jiggle_damping},
        {"jiggle_max_offset", &jiggle_max_offset},
        {"jiggle_translate_gain", &jiggle_translate_gain},
        {"jiggle_rotate_gain", &jiggle_rotate_gain},
        {"jiggle_bone_length", &jiggle_bone_length},
        {"jiggle_world_gain", &jiggle_world_gain},
    };

    for (auto& k : fkeys)
    {
        if (sect.size() && pSettings->line_exist(sect, k.key))
            *k.dst = pSettings->r_float(sect, k.key);
        else if (pSettings->line_exist("actor", k.key))
            *k.dst = pSettings->r_float("actor", k.key);
    }

    if (sect.size() && pSettings->line_exist(sect, "jiggle_axis"))
        jiggle_axis = pSettings->r_s32(sect, "jiggle_axis");
    else if (pSettings->line_exist("actor", "jiggle_axis"))
        jiggle_axis = pSettings->r_s32("actor", "jiggle_axis");

    clamp(jiggle_axis, 0, 2);
    jiggle_stiffness = _max(jiggle_stiffness, 0.0f);
    jiggle_damping = _max(jiggle_damping, 0.0f);
    jiggle_max_offset = _max(jiggle_max_offset, 0.0f);
    jiggle_bone_length = _max(jiggle_bone_length, EPS_L);
}

void CBoneSpringController::install(CActor* actor)
{
    remove();

    if (!actor)
        return;

    IKinematics* K = smart_cast<IKinematics*>(actor->Visual());
    if (!K)
        return;

    read_config(actor);

    LPCSTR bone_list = JIGGLE_DEFAULT_BONES;
    CCustomOutfit* outfit = actor->GetOutfit();
    if (outfit && pSettings->section_exist(outfit->cNameSect()) &&
        pSettings->line_exist(outfit->cNameSect(), "jiggle_bones"))
        bone_list = pSettings->r_string(outfit->cNameSect(), "jiggle_bones");
    else if (pSettings->line_exist("actor", "jiggle_bones"))
        bone_list = pSettings->r_string("actor", "jiggle_bones");

    const int count = _GetItemCount(bone_list);
    for (int i = 0; i < count && m_bone_count < MAX_SPRING_BONES; ++i)
    {
        string128 name;
        _GetItem(bone_list, i, name);

        const u16 bid = K->LL_BoneID(name);
        if (bid == BI_NONE)
            continue; // this outfit simply does not have the bone - not an error

        CBoneInstance& bi = K->LL_GetBoneInstance(bid);
        if (bi.callback())
        {
            // There is only one callback slot per bone. Something else owns this one
            // (CActor::SetCallbacks takes spine / spine1 / spine2 / head), so leave it
            // alone rather than silently breaking it.
            Msg("! [jiggle] bone [%s] already has a callback, skipping", name);
            continue;
        }

        bone_spring& b = m_bones[m_bone_count];
        b.owner = this;
        b.bone_id = bid;
        b.sim_pos.set(0.0f, 0.0f, 0.0f);
        b.velocity.set(0.0f, 0.0f, 0.0f);
        b.applied.set(0.0f, 0.0f, 0.0f);
        b.last_frame = 0;
        b.primed = false;

        ++m_bone_count;
    }

    if (!m_bone_count)
        return;

    // Install only once the array is fully built, so the pointers handed to the
    // callbacks stay valid.
    for (u16 i = 0; i < m_bone_count; ++i)
        K->LL_GetBoneInstance(m_bones[i].bone_id).set_callback(bctCustom, bone_callback, &m_bones[i], FALSE);

    m_actor = actor;
    m_visual = K;
    m_world_primed = false;
    m_accel_frame = 0;
}

void CBoneSpringController::remove()
{
    if (m_visual)
    {
        for (u16 i = 0; i < m_bone_count; ++i)
        {
            CBoneInstance& bi = m_visual->LL_GetBoneInstance(m_bones[i].bone_id);
            if (bi.callback() == bone_callback && bi.callback_param() == &m_bones[i])
                bi.reset_callback();
        }
    }

    detach();
}

void CBoneSpringController::detach()
{
    m_visual = nullptr;
    m_actor = nullptr;
    m_bone_count = 0;
    m_world_primed = false;
    m_accel_frame = 0;
}

// Whole-body acceleration (jumping, landing, lifts) expressed in model space. The bone
// spring runs in model space, which is a non-inertial frame, so this enters the
// integration as a pseudo-force of -a.
void CBoneSpringController::update_external_accel()
{
    if (m_accel_frame == Device.dwFrame)
        return;
    m_accel_frame = Device.dwFrame;

    if (!m_actor || fis_zero(jiggle_world_gain))
    {
        m_external_accel.set(0.0f, 0.0f, 0.0f);
        return;
    }

    const Fmatrix& X = m_actor->XFORM();
    const float dt = _max(Device.fTimeDelta, EPS_S);

    if (!m_world_primed)
    {
        m_prev_world_pos.set(X.c);
        m_prev_world_vel.set(0.0f, 0.0f, 0.0f);
        m_external_accel.set(0.0f, 0.0f, 0.0f);
        m_world_primed = true;
        return;
    }

    Fvector d;
    d.sub(X.c, m_prev_world_pos);
    m_prev_world_pos.set(X.c);

    if (d.magnitude() > JIGGLE_SNAP_DIST)
    {
        // teleport - drop the history instead of generating a huge impulse
        m_prev_world_vel.set(0.0f, 0.0f, 0.0f);
        m_external_accel.set(0.0f, 0.0f, 0.0f);
        return;
    }

    Fvector vel;
    vel.set(d).mul(1.0f / dt);

    Fvector accel;
    accel.sub(vel, m_prev_world_vel).mul(1.0f / dt);
    m_prev_world_vel.set(vel);

    const float a = accel.magnitude();
    if (a > JIGGLE_MAX_EXTERNAL_ACCEL)
        accel.mul(JIGGLE_MAX_EXTERNAL_ACCEL / a);

    // world -> model space; XFORM is orthonormal, so the inverse rotation is the transpose
    m_external_accel.set(accel.dotproduct(X.i), accel.dotproduct(X.j), accel.dotproduct(X.k));
    m_external_accel.mul(jiggle_world_gain);
}

void CBoneSpringController::integrate(bone_spring& b, CBoneInstance* bi)
{
    const Fvector target = bi->mTransform.c;

    if (!b.primed)
    {
        b.sim_pos.set(target);
        b.velocity.set(0.0f, 0.0f, 0.0f);
        b.applied.set(0.0f, 0.0f, 0.0f);
        b.primed = true;
        return;
    }

    Fvector delta;
    delta.sub(target, b.sim_pos);
    if (delta.magnitude() > JIGGLE_SNAP_DIST)
    {
        // visual swap, teleport, ragdoll snap - restart rather than catapult
        b.sim_pos.set(target);
        b.velocity.set(0.0f, 0.0f, 0.0f);
        b.applied.set(0.0f, 0.0f, 0.0f);
        return;
    }

    update_external_accel();

    float dt = Device.fTimeDelta;
    clamp(dt, 0.0f, JIGGLE_MAX_DT);
    if (dt <= 0.0f)
        return;

    int steps = int(ceilf(dt / JIGGLE_SUBSTEP));
    clamp(steps, 1, JIGGLE_MAX_SUBSTEPS);
    const float h = dt / float(steps);

    for (int s = 0; s < steps; ++s)
    {
        Fvector accel;
        accel.sub(target, b.sim_pos).mul(jiggle_stiffness);
        accel.mad(b.velocity, -jiggle_damping);
        accel.sub(m_external_accel); // body accelerates up -> flesh lags down

        b.velocity.mad(accel, h);
        b.sim_pos.mad(b.velocity, h);
    }

    // lag = how far behind the animated position the simulation is
    Fvector lag;
    lag.sub(b.sim_pos, target);

    const float len = lag.magnitude();
    if (len > jiggle_max_offset)
    {
        lag.mul(jiggle_max_offset / len);
        // keep the simulation consistent with what is actually drawn, otherwise the
        // spring keeps integrating energy it never gets to release
        b.sim_pos.add(target, lag);
    }

    b.applied.set(lag);
}

void CBoneSpringController::apply(bone_spring& b, CBoneInstance* bi)
{
    const float len = b.applied.magnitude();
    if (len < EPS_L)
        return;

    Fmatrix& M = bi->mTransform;

    // Swing: rotate the bone about its own origin so its length axis tilts toward the
    // lag. Bone matrices out of the animation blender are orthonormal, so expressing
    // the lag in the bone's frame is just three dot products.
    if (!fis_zero(jiggle_rotate_gain))
    {
        Fvector local;
        local.set(b.applied.dotproduct(M.i), b.applied.dotproduct(M.j), b.applied.dotproduct(M.k));

        Fvector len_axis;
        switch (jiggle_axis)
        {
        case 0: len_axis.set(1.0f, 0.0f, 0.0f); break;
        case 1: len_axis.set(0.0f, 1.0f, 0.0f); break;
        default: len_axis.set(0.0f, 0.0f, 1.0f); break;
        }

        Fvector axis;
        axis.crossproduct(len_axis, local);
        if (axis.magnitude() > EPS_L)
        {
            axis.normalize();
            const float angle = atanf(len / jiggle_bone_length) * jiggle_rotate_gain;

            Fmatrix R;
            R.rotation(axis, angle);

            // mul_43(A, B) applies B first and then A, so this rotates within the bone's
            // own frame about its own origin and leaves M.c untouched.
            Fmatrix out;
            out.mul_43(M, R);
            M.set(out);
        }
    }

    if (!fis_zero(jiggle_translate_gain))
        M.c.mad(b.applied, jiggle_translate_gain);
}

void _BCL CBoneSpringController::bone_callback(CBoneInstance* bi)
{
    bone_spring* b = static_cast<bone_spring*>(bi->callback_param());
    if (!b || !b->owner || !jiggle_enabled)
        return;

    // CalculateBones runs more than once per frame on the actor: once for the render
    // pass, and again from player_legs_controller::copy_bones_from_actor, which passes
    // bForceExact and so skips the early-out in CKinematics::CalculateBones. Integrate
    // on the first call of a frame only, then re-apply the cached displacement - the
    // animated pose is rebuilt from scratch by BuildBoneMatrix on every call, so the
    // offset has to be re-applied even when it is not recomputed.
    if (b->last_frame != Device.dwFrame && !Device.Paused())
    {
        b->last_frame = Device.dwFrame;
        b->owner->integrate(*b, bi);
    }

    b->owner->apply(*b, bi);
}
