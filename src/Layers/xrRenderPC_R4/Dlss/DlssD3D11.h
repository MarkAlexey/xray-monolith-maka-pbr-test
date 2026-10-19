#pragma once

#include "DlssTemporal.h"
#include <d3d11_1.h>
#include <wrl/client.h>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace dlss
{
// Device lifetime, not render-target lifetime. Call ReleaseFeature on vid_restart
// and Shutdown BEFORE releasing the D3D11 device. No FG/Streamline dependency.
class D3D11Backend
{
public:
    using Log = void (*)(const char* stage, unsigned result);
    D3D11Backend() = default;
    ~D3D11Backend();
    D3D11Backend(const D3D11Backend&) = delete;
    D3D11Backend& operator=(const D3D11Backend&) = delete;

    bool Initialize(ID3D11Device* device, const wchar_t* cachePath, Log log);
    bool OptimalSize(Quality quality, Size display, Size& render);
    bool Create(Quality quality, Size render, Size display, bool hdr, Preset preset = Preset::Default);
    void ReleaseFeature();
    void Shutdown();

    struct Frame
    {
        ID3D11Texture2D* color = nullptr;
        ID3D11Texture2D* depth = nullptr; // Normalized hardware depth, NOT view-space Z.
        ID3D11Texture2D* motion = nullptr; // SSS 23 non-jittered UV displacement.
        Offset jitter;
        float deltaMilliseconds = 0;
        std::uint64_t frame = 0;
        bool cameraCut = false;
    };
    bool Evaluate(const Frame& frame);
    void ResetHistory() { m_history.Invalidate(); }
    bool Ready() const { return m_feature != nullptr && !m_failed; }
    ID3D11Texture2D* Output() const { return m_output.Get(); }
    ID3D11ShaderResourceView* OutputView() const { return m_outputView.Get(); }
    Size RenderSize() const { return m_render; }
    Size DisplaySize() const { return m_display; }

private:
    bool Check(unsigned result, const char* stage);
    bool Validate(const Frame& frame) const;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_context;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> m_state;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_output;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputView;
    NVSDK_NGX_Parameter* m_parameters = nullptr;
    NVSDK_NGX_Handle* m_feature = nullptr;
    Log m_log = nullptr;
    Size m_render, m_display;
    History m_history;
    bool m_initialized = false, m_failed = false;
};
}
