#pragma once
#include <d3d11.h>

namespace aoe_render
{
inline bool shadow_stencil_neutral(const D3D11_DEPTH_STENCIL_DESC& depth)
{
    if (!depth.StencilEnable) return true;
    const auto inert_face = [](const D3D11_DEPTH_STENCILOP_DESC& face)
    {
        return face.StencilFunc == D3D11_COMPARISON_ALWAYS &&
            face.StencilFailOp == D3D11_STENCIL_OP_KEEP &&
            face.StencilDepthFailOp == D3D11_STENCIL_OP_KEEP &&
            face.StencilPassOp == D3D11_STENCIL_OP_KEEP;
    };
    return inert_face(depth.FrontFace) && inert_face(depth.BackFace);
}

inline bool shadow_batch_state_safe(const D3D11_DEPTH_STENCIL_DESC& depth, const D3D11_BLEND_DESC& blend)
{
    if (!depth.DepthEnable || depth.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL ||
        (depth.DepthFunc != D3D11_COMPARISON_LESS && depth.DepthFunc != D3D11_COMPARISON_LESS_EQUAL) ||
        blend.AlphaToCoverageEnable || !shadow_stencil_neutral(depth)) return false;
    for (const auto& target : blend.RenderTarget) if (target.BlendEnable) return false;
    return true;
}
}
