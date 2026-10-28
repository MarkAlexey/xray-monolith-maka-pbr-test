#include "stdafx.h"
#include "scope_glass_head.h"
#include "scope_vslr_renderer.h"
#include <iterator>

namespace
{
// Spheres in metres at the reference head radius, as offsets back from the eye, up and to the right.
// Head spheres follow the view; body spheres follow only the heading, as the body stays upright when
// the view pitches. The eye sits just outside the head's front surface.
struct BodySphere
{
    bool head;
    float back, up, right, radius;
};
constexpr float reference_head = .09f;
constexpr BodySphere figure[] = {
    {true, .10f, .02f, 0.f, .095f},     // cranium
    {true, .045f, -.065f, 0.f, .065f},  // jaw
    {false, .10f, -.15f, 0.f, .06f},    // neck
    {false, .12f, -.30f, -.19f, .085f}, // shoulders, left to right
    {false, .12f, -.28f, -.10f, .09f},
    {false, .12f, -.27f, 0.f, .09f},
    {false, .12f, -.28f, .10f, .09f},
    {false, .12f, -.30f, .19f, .085f},
    {false, .10f, -.44f, -.11f, .13f},  // chest
    {false, .10f, -.44f, .11f, .13f},
};
static_assert(std::size(figure) == 10, "SCOPE_GLASS_BODY_SPHERES in scope_glass_head.h");
}

Fvector scope_glass_shooter_eye(const Fvector& eye, const Fvector& right, const Fvector& up, const Fvector& forward)
{
    Fvector moved = eye;
    moved.mad(right, ps_r__svp_lens_shooter_x).mad(up, ps_r__svp_lens_shooter_y).mad(forward, ps_r__svp_lens_shooter_z);
    return moved;
}

bool scope_glass_shooter_shown(bool aiming)
{
    return ps_r__svp_lens_shooter && (aiming || ps_r__svp_lens_shooter_hip) && ps_r__svp_lens_head > 0.f;
}

void scope_glass_head_bind(bool aiming)
{
    const bool shown = scope_glass_shooter_shown(aiming);
    const float head = shown ? ps_r__svp_lens_head : 0.f;
    const float scale = head / reference_head;
    const Fvector& forward = Device.vCameraDirection;
    const Fvector world_up = {0.f, 1.f, 0.f};
    // Looking straight up or down, the camera's top points along the heading.
    Fvector heading = {forward.x, 0.f, forward.z};
    if (heading.square_magnitude() < 1e-6f)
        heading.set(Device.vCameraTop.x * -forward.y, 0.f, Device.vCameraTop.z * -forward.y);
    heading.normalize_safe();
    Fvector side;
    side.crossproduct(world_up, heading);
    const Fvector eye = scope_glass_shooter_eye(Device.vCameraPosition, Device.vCameraRight, Device.vCameraTop, forward);
    for (u32 i = 0; i < std::size(figure); ++i)
    {
        const BodySphere& sphere = figure[i];
        Fvector centre = eye;
        centre.mad(sphere.head ? forward : heading, -sphere.back * scale);
        centre.mad(sphere.head ? Device.vCameraTop : world_up, sphere.up * scale);
        centre.mad(sphere.head ? Device.vCameraRight : side, sphere.right * scale);
        RCache.set_ca("scope_glass_body", i, centre.x, centre.y, centre.z, sphere.radius * scale);
    }
    RCache.set_c("scope_glass_head_params", ps_r__svp_lens_head_albedo, shown ? 1.f : 0.f, ps_r__svp_lens_shooter_ads, 0.f);
    // The player's body model, where its mirror image is current; the glass finds it along each reflected
    // ray (scope_glass_mirror_ray).
    Fvector4 mirror_origin{}, mirror_right{}, mirror_up{}, mirror_forward{}, mirror_head{};
    if (!shown
        || !scope_vslr_mirror_view(eye, forward, Device.vCameraTop, mirror_origin, mirror_right, mirror_up, mirror_forward,
            mirror_head))
        mirror_origin.set(0.f, 0.f, 0.f, 0.f);
    RCache.set_c("scope_glass_mirror_origin", mirror_origin);
    RCache.set_c("scope_glass_mirror_right", mirror_right);
    RCache.set_c("scope_glass_mirror_up", mirror_up);
    RCache.set_c("scope_glass_mirror_forward", mirror_forward);
    RCache.set_c("scope_glass_mirror_head", mirror_head);
}
