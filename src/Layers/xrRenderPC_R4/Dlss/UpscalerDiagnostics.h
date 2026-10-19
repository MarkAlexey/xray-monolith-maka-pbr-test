#pragma once

#include "DlssD3D11.h"
#include <cstdio>

namespace dlss
{
// Failure path only: preserve the inputs needed to diagnose validation/SDK errors.
inline void LogEvaluationInputs(const D3D11Backend::Frame& frame, Size render,
    ID3D11Texture2D* output, D3D11Backend::Log log,
    unsigned result = unsigned(E_INVALIDARG))
{
    if (!log) return;
    // Use the same resource-device reference as validation, including with ReShade.
    Microsoft::WRL::ComPtr<ID3D11Device> resourceDevice;
    if (output) output->GetDevice(&resourceDevice);
    char message[512];
    std::snprintf(message, sizeof(message),
        "Evaluation inputs: frame=%llu expected=%ux%u jitter=(%.9g,%.9g) delta_ms=%.9g; invalid_jitter=%u invalid_time=%u color_aliases_output=%u missing_output=%u",
        static_cast<unsigned long long>(frame.frame), render.width, render.height,
        double(frame.jitter.x), double(frame.jitter.y), double(frame.deltaMilliseconds),
        unsigned(!std::isfinite(frame.jitter.x) || !std::isfinite(frame.jitter.y)),
        unsigned(!std::isfinite(frame.deltaMilliseconds) || frame.deltaMilliseconds < 0),
        unsigned(frame.color && frame.color == output), unsigned(!output));
    log(message, result);

    ID3D11Texture2D* inputs[] = {frame.color, frame.depth, frame.motion};
    const char* names[] = {"color", "depth", "motion"};
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!inputs[i])
        {
            std::snprintf(message, sizeof(message), "Evaluation input %s: NULL", names[i]);
            log(message, result);
            continue;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        inputs[i]->GetDesc(&desc);
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        inputs[i]->GetDevice(&device);
        const bool validFormat = i == 0 || (i == 1
            ? (desc.Format == DXGI_FORMAT_R32_FLOAT || desc.Format == DXGI_FORMAT_R32_TYPELESS ||
                desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
            : (desc.Format == DXGI_FORMAT_R16G16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                desc.Format == DXGI_FORMAT_R32G32_FLOAT));
        std::snprintf(message, sizeof(message),
            "Evaluation input %s: size=%ux%u format=%u samples=%u array=%u bind=0x%X; invalid_size=%u invalid_format=%u invalid_samples=%u invalid_array=%u missing_srv=%u wrong_device=%u",
            names[i], desc.Width, desc.Height, unsigned(desc.Format), desc.SampleDesc.Count, desc.ArraySize, desc.BindFlags,
            unsigned(desc.Width != render.width || desc.Height != render.height), unsigned(!validFormat),
            unsigned(desc.SampleDesc.Count != 1), unsigned(desc.ArraySize != 1),
            unsigned(!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)), unsigned(device.Get() != resourceDevice.Get()));
        log(message, result);
    }
}
}
