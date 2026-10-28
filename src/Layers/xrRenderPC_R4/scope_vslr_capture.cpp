#include "scope_vslr_capture.h"
#include <cassert>
#include <cmath>
#include <utility>

namespace scope_vslr
{
static unsigned mip_levels(unsigned size)
{
    unsigned levels = 1;
    while (size > 1)
    {
        size /= 2;
        ++levels;
    }
    return levels;
}

bool Capture::create_cube(ID3D11Device* device, unsigned size, DXGI_FORMAT format, bool mipmapped, Cube& cube)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = size;
    desc.MipLevels = mipmapped ? mip_levels(size) : 1;
    desc.ArraySize = 6;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | (mipmapped ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);
    if (FAILED(device->CreateTexture2D(&desc, nullptr, cube.texture.GetAddressOf())))
        return false;
    if (FAILED(device->CreateShaderResourceView(cube.texture.Get(), nullptr, cube.resource.GetAddressOf())))
        return false;

    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = format;
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.ArraySize = 1;
    for (unsigned face = 0; face < 6; ++face)
    {
        view.Texture2DArray.FirstArraySlice = face;
        if (FAILED(device->CreateRenderTargetView(cube.texture.Get(), &view, cube.faces[face].GetAddressOf())))
            return false;
    }
    return true;
}

bool Capture::create(ID3D11Device* device, Settings settings)
{
    // Reject invalid requests at the allocation boundary, not during each draw.
    if (!device || settings.resolution < 32 || settings.resolution > 512 ||
        !std::isfinite(settings.radius) || settings.radius < 1.f || settings.radius > 100.f ||
        settings.faces_per_frame < 1 || settings.faces_per_frame > 6)
        return false;
    std::array<Buffer, 3> next;
    for (auto& buffer : next)
        if (!create_cube(device, settings.resolution, DXGI_FORMAT_R11G11B10_FLOAT, true, buffer.colour) ||
            !create_cube(device, settings.resolution, DXGI_FORMAT_R16_FLOAT, false, buffer.distance) ||
            !create_cube(device, settings.resolution, DXGI_FORMAT_R8_UNORM, true, buffer.coverage) ||
            !create_cube(device, settings.resolution, DXGI_FORMAT_R11G11B10_FLOAT, true, buffer.direct))
            return false;
    buffers_ = std::move(next);
    settings_ = settings;
    published_ = -1;
    previous_ = -1;
    writing_ = 0;
    next_face_ = 6;
    return true;
}

void Capture::begin(Position origin)
{
    assert(!capturing());
    pending_origin_ = origin;
    next_face_ = 0;
}

ID3D11RenderTargetView* Capture::colour_target() const
{
    assert(capturing());
    return buffers_[writing_].colour.faces[next_face_].Get();
}

ID3D11RenderTargetView* Capture::distance_target() const
{
    assert(capturing());
    return buffers_[writing_].distance.faces[next_face_].Get();
}

ID3D11RenderTargetView* Capture::coverage_target() const
{
    assert(capturing());
    return buffers_[writing_].coverage.faces[next_face_].Get();
}

ID3D11RenderTargetView* Capture::direct_target() const
{
    assert(capturing());
    return buffers_[writing_].direct.faces[next_face_].Get();
}

void Capture::finish_face()
{
    assert(capturing());
    if (++next_face_ == 6)
    {
        previous_ = published_;
        previous_origin_ = published_origin_;
        published_ = int(writing_);
        published_origin_ = pending_origin_;
        // The next capture writes the generation neither published nor previous.
        for (unsigned buffer = 0; buffer < buffers_.size(); ++buffer)
            if (int(buffer) != published_ && int(buffer) != previous_)
                writing_ = buffer;
    }
}

ID3D11ShaderResourceView* Capture::colour() const
{
    return ready() ? buffers_[published_].colour.resource.Get() : nullptr;
}

ID3D11ShaderResourceView* Capture::distance() const
{
    return ready() ? buffers_[published_].distance.resource.Get() : nullptr;
}

ID3D11ShaderResourceView* Capture::coverage() const
{
    return ready() ? buffers_[published_].coverage.resource.Get() : nullptr;
}

ID3D11ShaderResourceView* Capture::direct() const
{
    return ready() ? buffers_[published_].direct.resource.Get() : nullptr;
}

ID3D11ShaderResourceView* Capture::previous_colour() const
{
    return has_previous() ? buffers_[previous_].colour.resource.Get() : nullptr;
}

ID3D11ShaderResourceView* Capture::previous_coverage() const
{
    return has_previous() ? buffers_[previous_].coverage.resource.Get() : nullptr;
}

unsigned Capture::texture_bytes() const
{
    // Three generations, six faces, 4-byte HDR colour and direct light and 1-byte coverage with their mip
    // chains and 2-byte radial distance. Excludes driver allocation overhead and the shared raster
    // depth/G-buffer targets.
    unsigned texels = 0;
    for (unsigned size = settings_.resolution; size; size /= 2)
        texels += size * size;
    return unsigned(buffers_.size()) * 6 * (texels * (4 + 4 + 1) + settings_.resolution * settings_.resolution * 2);
}
}
