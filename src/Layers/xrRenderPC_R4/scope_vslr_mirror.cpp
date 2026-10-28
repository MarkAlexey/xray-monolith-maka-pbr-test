#include "stdafx.h"
#include "scope_vslr_mirror.h"
#include "scope_vslr_renderer.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/bone.h"
#include "../../Include/xrRender/Kinematics.h"

namespace scope_vslr
{
namespace
{
// A camera this far in front of the eye looks back at the body model, between the face and the aiming
// arms. The glass finds the body on a plane mirror_face_m in front of the eye.
constexpr float mirror_distance_m = .18f;
constexpr float mirror_near_m = .02f;
constexpr float mirror_face_m = .01f;
// Half the extent the image covers at the face. Aiming, the eyepiece just in front of the eye reflects the
// face close up; at the hip the glass further off reflects head, shoulders and chest.
constexpr float mirror_field_aim_m = .15f;
constexpr float mirror_field_hip_m = .5f;
// Each image blends over the one before with this time constant, as captures blend between generations.
constexpr float mirror_fade_s = .05f;
// The body model's eyes from its head bone (the base of the skull), up and forward along the camera.
constexpr float model_eye_up_m = .08f;
constexpr float model_eye_forward_m = .09f;

// The camera for a view: in front of the eye looking back at it, upright with the view.
struct Frame
{
    Fvector origin, right, up, forward;
};
Frame frame_of(const Fvector& eye, const Fvector& view_forward, const Fvector& view_up)
{
    Frame frame;
    frame.origin.mad(eye, view_forward, mirror_distance_m);
    frame.forward.invert(view_forward);
    frame.up = view_up;
    frame.right.crossproduct(frame.up, frame.forward);
    return frame;
}
}

// The images keep a mip chain so blurred reflections of the player blur alike.
bool Mirror::create(unsigned size)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = size;
    desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    bool created = true;
    for (Image& image : images_)
        created = created && SUCCEEDED(HW.pDevice->CreateTexture2D(&desc, nullptr, image.image.GetAddressOf()))
            && SUCCEEDED(HW.pDevice->CreateRenderTargetView(image.image.Get(), nullptr, image.target.GetAddressOf()))
            && SUCCEEDED(HW.pDevice->CreateShaderResourceView(image.image.Get(), nullptr, image.view.GetAddressOf()));
    if (!created)
    {
        for (Image& image : images_)
            image = {};
        return false;
    }
    alias_.create(mirror_texture);
    alias_->surface_set(images_[0].image.Get());
    previous_alias_.create(mirror_previous_texture);
    previous_alias_->surface_set(images_[1].image.Get());
    return true;
}

void Mirror::release()
{
    for (ref_texture* texture : {&alias_, &previous_alias_})
        if (*texture) (*texture)->surface_set(nullptr);
}

Fmatrix& Mirror::place_body(IRenderable& viewer, const Fvector& eye, const Fvector& forward, const Fvector& up)
{
    body_ = viewer.renderable.xform;
    auto* kinematics = viewer.renderable.visual->dcast_PKinematics();
    if (!kinematics) return body_;
    kinematics->CalculateBones(TRUE);
    const u16 head = kinematics->LL_BoneID("bip01_head");
    if (head == BI_NONE) return body_;
    Fvector model_eye, offset;
    viewer.renderable.xform.transform_tiny(model_eye, kinematics->LL_GetTransform(head).c);
    model_eye.mad(up, model_eye_up_m).mad(forward, model_eye_forward_m);
    offset.sub(eye, model_eye);
    body_.c.add(offset);
    // r__scope_vslr_debug 2: how far the body was moved, in the camera's frame (right, up, forward).
    if (ps_scope_vslr_debug == 2 && Device.dwTimeGlobal >= next_report_ms_)
    {
        next_report_ms_ = Device.dwTimeGlobal + 1000;
        Fvector right;
        right.crossproduct(up, forward);
        Msg("[VSLR-MIRROR] body moved: right=%.3f up=%.3f forward=%.3f m", offset.dotproduct(right),
            offset.dotproduct(up), offset.dotproduct(forward));
    }
    return body_;
}

Mirror::Pass Mirror::begin(const Fvector& eye, const Fvector& forward, const Fvector& up, bool aiming)
{
    Pass pass;
    const Frame frame = frame_of(eye, forward, up);
    pass.view.build_camera_dir(frame.origin, frame.forward, frame.up);
    // The image spans what a reflected ray can meet.
    const float tan = (aiming ? mirror_field_aim_m : mirror_field_hip_m) / (mirror_distance_m - mirror_face_m);
    // The image is fixed to the eye, so the one before blends in unless it was not drawn last frame or
    // spans another field.
    const bool continuous = frame_ + 1 == Device.dwFrame && tan == tan_;
    tan_ = tan;
    previous_alias_->surface_set(images_[current_].image.Get());
    pass.projection.build_projection(2.f * atanf(tan_), 1.f, mirror_near_m, mirror_far_m);
    pass.weight = continuous ? 1.f - expf(-Device.fTimeDelta / mirror_fade_s) : 1.f;
    return pass;
}

void Mirror::finish()
{
    const unsigned next = 1 - current_;
    HW.pContext->GenerateMips(images_[next].view.Get());
    // Read by the glass next frame; the renderer's restore that follows resets the bind cache.
    alias_->surface_set(images_[next].image.Get());
    current_ = next;
    frame_ = Device.dwFrame;
}

bool Mirror::view(const Fvector& eye, const Fvector& view_forward, const Fvector& view_up, Fvector4& origin,
    Fvector4& right, Fvector4& up, Fvector4& forward, Fvector4& head) const
{
    // The glass reads last frame's image: the mirror is drawn after the combine. The image is fixed to the
    // eye, so it is placed by this frame's camera, without the frame of lag the camera it was drawn from has.
    if (Device.dwFrame - frame_ > 1) return false;
    const Frame frame = frame_of(eye, view_forward, view_up);
    origin.set(frame.origin.x, frame.origin.y, frame.origin.z, 1.f);
    right.set(frame.right.x, frame.right.y, frame.right.z, 0.f);
    up.set(frame.up.x, frame.up.y, frame.up.z, 0.f);
    forward.set(frame.forward.x, frame.forward.y, frame.forward.z, tan_);
    head.set(eye.x, eye.y, eye.z, mirror_face_m);
    return true;
}

// The newest image's first level, three seconds after the first image (the aim has settled), as raw RGBA
// half floats.
void Mirror::dump()
{
    static bool dumped = false;
    static u32 first_ms = 0;
    if (dumped) return;
    if (!first_ms) first_ms = Device.dwTimeGlobal;
    if (Device.dwTimeGlobal - first_ms < 3000) return;
    // One attempt: a failure is not retried every frame.
    dumped = true;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D* image = images_[current_].image.Get();
    image->GetDesc(&desc);
    desc.MipLevels = desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(HW.pDevice->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return;
    HW.pContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, image, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(HW.pContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
    if (IWriter* file = FS.w_open("$logs$", "vslr_mirror_rgba16f.raw"))
    {
        for (u32 row = 0; row < desc.Height; ++row)
            file->w(static_cast<const u8*>(mapped.pData) + row * mapped.RowPitch, desc.Width * 8);
        FS.w_close(file);
        Msg("[VSLR-MIRROR] mirror image %ux%u written to the logs folder", desc.Width, desc.Height);
    }
    HW.pContext->Unmap(staging.Get(), 0);
}
}
