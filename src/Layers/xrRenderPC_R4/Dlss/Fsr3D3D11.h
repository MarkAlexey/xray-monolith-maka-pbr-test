#pragma once
#include "DlssD3D11.h"
#include "../../../../sdk/FSR3/include/FidelityFX/host/ffx_fsr3upscaler.h"
#include <vector>
#include <memory>

namespace fsr3
{
// Native D3D11 backend, no DX12 bridge, NGX or frame generation.
class D3D11Backend
{
public:
    using Log = dlss::D3D11Backend::Log;
    struct Frame : dlss::D3D11Backend::Frame
    {
        float cameraNear = 0.1f, cameraFar = 1000.0f, verticalFov = 1.0f;
    };
    ~D3D11Backend() { Destroy(); }
    static bool OptimalSize(unsigned quality, dlss::Size display, dlss::Size& render);
    bool Create(ID3D11Device* device, dlss::Size render, dlss::Size display, bool hdr, Log log);
    void Destroy();
    bool Evaluate(const Frame& frame);
    void ResetHistory() { m_history.Invalidate(); }
    bool Ready() const { return m_created && !m_failed; }
    ID3D11Texture2D* Output() const { return m_output.Get(); }
private:
    bool Check(FfxErrorCode result, const char* stage);
    bool Validate(const Frame& frame) const;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_context;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> m_state;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplers[2];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_output, m_shared[3];
    // SDK context is 512 KiB: keep it off the engine/test thread stack.
    std::unique_ptr<FfxFsr3UpscalerContext> m_fsr;
    std::vector<uint64_t> m_scratch; // SDK requires aligned, zero-initialized storage.
    dlss::Size m_render, m_display;
    dlss::History m_history;
    Log m_log = nullptr;
    bool m_created = false, m_failed = false;
};
}
