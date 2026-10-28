#include "stdafx.h"
#include "svp_frame_burst.h"
#include "../xrRender/svp_console.h"

namespace
{
// One frame from the mapped back buffer, every r__svp_frame_burst_scale-th pixel of every such row, as RGBA8.
void write_frame(const D3D11_MAPPED_SUBRESOURCE& mapped, const D3D11_TEXTURE2D_DESC& desc, u32 index)
{
    const u32 burst_scale = u32(ps_r__svp_frame_burst_scale);
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const u32 width = desc.Width / burst_scale, height = desc.Height / burst_scale;
    string_path name;
    xr_sprintf(name, "burst\\frame_%04u_%ux%u.rgba", index, width, height);
    IWriter* file = FS.w_open("$logs$", name);
    if (!file) return;
    xr_vector<u8> row(width * 4);
    for (u32 y = 0; y < height; ++y)
    {
        const u8* source = static_cast<const u8*>(mapped.pData) + y * burst_scale * mapped.RowPitch;
        for (u32 x = 0; x < width; ++x)
        {
            const u8* pixel = source + x * burst_scale * 4;
            row[x * 4 + 0] = pixel[bgra ? 2 : 0];
            row[x * 4 + 1] = pixel[1];
            row[x * 4 + 2] = pixel[bgra ? 0 : 2];
            row[x * 4 + 3] = 255;
        }
        file->w(row.data(), width * 4);
    }
    FS.w_close(file);
}
} // namespace

void svp_frame_burst_capture()
{
    static u32 index = 0;
    if (ps_r__svp_frame_burst <= 0) return;
    --ps_r__svp_frame_burst;

    ID3D11Resource* back = nullptr;
    HW.pBaseRT->GetResource(&back);
    ID3D11Texture2D* back_texture = static_cast<ID3D11Texture2D*>(back);
    D3D11_TEXTURE2D_DESC desc;
    back_texture->GetDesc(&desc);
    const bool eight_bit = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!eight_bit || desc.SampleDesc.Count != 1)
    {
        Msg("[SVP-BURST] back buffer format %u samples %u not supported", desc.Format, desc.SampleDesc.Count);
        ps_r__svp_frame_burst = 0;
        _RELEASE(back);
        return;
    }

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(HW.pDevice->CreateTexture2D(&staging_desc, nullptr, &staging)))
    {
        HW.pContext->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(HW.pContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            write_frame(mapped, desc, index);
            HW.pContext->Unmap(staging, 0);
            Msg("[SVP-BURST] index=%u frame=%u dt=%.2f", index, Device.dwFrame, Device.fTimeDelta * 1000.f);
            ++index;
        }
        _RELEASE(staging);
    }
    _RELEASE(back);
}
