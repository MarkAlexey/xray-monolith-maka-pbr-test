#include "stdafx.h"
#include "DlssIntegration.h"
#include "DlssD3D11.h"
#include "Fsr3D3D11.h"
#include "../../../xrEngine/igame_persistent.h"
#include "../../../xrEngine/RendererError.h"
#ifdef XR_DX12_BRIDGE
#include "../../xrRenderDX10/D3D12PresentBridge.h"
#endif

namespace
{
#ifdef XR_DX12_BRIDGE
// AMD FSR through the DX12 bridge: FSR 4 on Radeon RX 7000/9000, the runtime's 3.1
// fallback elsewhere. Owned by the bridge; this only tracks history and failure.
class FsrD3D12Backend
{
public:
    bool Create(dlss::Size render, dlss::Size display, bool hdr)
    {
        Destroy();
        auto* bridge = HW.PresentationBridge();
        if (!bridge || FAILED(bridge->CreateUpscaler(render.width, render.height, display.width, display.height, hdr)))
            return false;
        m_render = render;
        m_created = true;
        return true;
    }
    void Destroy()
    {
        if (m_created)
            if (auto* bridge = HW.PresentationBridge()) bridge->DestroyUpscaler();
        m_created = m_failed = false;
        m_history.Invalidate();
    }
    bool Evaluate(const fsr3::D3D11Backend::Frame& frame)
    {
        if (!Ready()) return false;
        UpscaleInputs inputs;
        inputs.jitterX = frame.jitter.x;
        inputs.jitterY = frame.jitter.y;
        const auto scale = dlss::MotionScale(m_render);
        inputs.motionScaleX = scale.x;
        inputs.motionScaleY = scale.y;
        inputs.frameTimeDelta = frame.deltaMilliseconds;
        inputs.cameraNear = frame.cameraNear;
        inputs.cameraFar = frame.cameraFar;
        inputs.verticalFov = frame.verticalFov;
        inputs.reset = m_history.Begin(frame.frame, frame.cameraCut);
        if (FAILED(HW.PresentationBridge()->Upscale(frame.color, frame.depth, frame.motion, inputs)))
        {
            m_failed = true;
            m_history.Invalidate();
            return false;
        }
        m_history.Commit(frame.frame);
        return true;
    }
    void ResetHistory() { m_history.Invalidate(); }
    bool Ready() const
    {
        auto* bridge = HW.PresentationBridge();
        return m_created && !m_failed && bridge && bridge->UpscalerReady();
    }
    ID3D11Texture2D* Output() const
    {
        auto* bridge = HW.PresentationBridge();
        return m_created && bridge ? bridge->UpscaleOutput() : nullptr;
    }
private:
    dlss::Size m_render;
    dlss::History m_history;
    bool m_created = false, m_failed = false;
};
FsrD3D12Backend fsrDx12Backend;
#endif

enum class Upscaler { Dlss, Fsr3, FsrDx12 };
dlss::D3D11Backend backend;
fsr3::D3D11Backend fsrBackend;
Upscaler method = Upscaler::Dlss;
Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneDepth;
Microsoft::WRL::ComPtr<ID3D11DepthStencilView> sceneDepthView;
dlss::Size renderSize;
dlss::Offset rasterJitter;
dlss::Offset previousJitter;
bool inScene = false, mainView = false, configured = false;
unsigned jitterIndex = 0;
Fvector lastPosition, lastDirection;
float lastFov = 0;
bool haveCamera = false;
bool reportedEvaluation = false;
string512 backendError = {};

const char* Method()
{
    switch (method)
    {
    case Upscaler::Fsr3: return "FSR3";
    case Upscaler::FsrDx12: return "FSR";
    default: return "DLSS";
    }
}
bool UsesFsr() { return method != Upscaler::Dlss; }
void ShowBackendError(const char* method)
{
    string128 summary;
    xr_sprintf(summary, "%s is not active.", method);
    SetRendererError(summary, "See the console/log for details.");
}
void LogBackendError(const char* method, const char* stage, unsigned result)
{
    xr_sprintf(backendError, "%s (0x%08X)", stage, result);
    Msg("! [%s] %s", method, backendError);
    ShowBackendError(method);
}
void LogDlss(const char* stage, unsigned result) { LogBackendError("DLSS", stage, result); }
void LogFsr(const char* stage, unsigned result) { LogBackendError("FSR3", stage, result); }
void ActivationFailed(const char* reason, const char* recovery)
{
    string128 summary;
    xr_sprintf(summary, "[%s] NOT ACTIVE - rendering at native resolution", Method());
    Msg("! %s: %s", summary, reason);
    Msg("! [%s] %s", Method(), recovery);
    ShowBackendError(Method());
}
const char* BackendReason(const char* fallback) { return backendError[0] ? backendError : fallback; }
int dlssSupport = -1; // Per-device NGX probe: -1 unknown, 0 unavailable, 1 available.
bool NgxCachePath(wchar_t (&wide)[MAX_PATH])
{
    string_path cache;
    FS.update_path(cache, "$app_data_root$", "");
    return MultiByteToWideChar(CP_ACP, 0, cache, -1, wide, MAX_PATH) != 0;
}
// Logs without the renderer-error overlay: a probe is not an activation failure.
void LogProbe(const char* stage, unsigned result) { Msg("- [DLSS] Support probe: %s (0x%08X)", stage, result); }
void ResetHistory()
{
    backend.ResetHistory();
    fsrBackend.ResetHistory();
#ifdef XR_DX12_BRIDGE
    fsrDx12Backend.ResetHistory();
#endif
}
bool BackendReady()
{
    switch (method)
    {
    case Upscaler::Fsr3: return fsrBackend.Ready();
#ifdef XR_DX12_BRIDGE
    case Upscaler::FsrDx12: return fsrDx12Backend.Ready();
#endif
    default: return backend.Ready();
    }
}
}

unsigned RenderScreenWidth() { return inScene ? dlss::RenderWidth() : Device.dwWidth; }
unsigned RenderScreenHeight() { return inScene ? dlss::RenderHeight() : Device.dwHeight; }

namespace dlss
{
void InitializeTargets()
{
    PROF_EVENT("Render.Upscaler.InitializeTargets");
    ReleaseTargets();
    renderSize = {Device.dwWidth, Device.dwHeight};
    backendError[0] = 0;
    if (!ps_r_upscaler || (ps_r_upscaler == 1 && !ps_r_dlss_quality))
    {
        SetRendererError(nullptr);
        Msg("[Upscaler] Off: rendering at native resolution.");
        return;
    }
    method = ps_r_upscaler == 2 ? Upscaler::Fsr3 : ps_r_upscaler == 3 ? Upscaler::FsrDx12 : Upscaler::Dlss;
    string_path marker;
    for (const char* shader : {"r3\\screenspace_mvectors.h", "r3\\dlss_contract.h", "r3\\dlss_ui_depth.ps",
        "r3\\dlss_copy.ps", "r3\\dlss_sharpen.ps", "r3\\dlss_cas.h"})
    {
        if (FS.exist(marker, "$game_shaders$", shader)) continue;
        string256 reason;
        xr_sprintf(reason, "Required shader is missing: %s", shader);
        ActivationFailed(reason, "Install the shader compatibility patch matching this engine, then restart the game.");
        return;
    }
    if (!RImplementation.o.ssfx_motionvectors)
    {
        ActivationFailed("SSS motion vectors are unavailable.",
            "Install the matching SSS 23 shader compatibility patch, then restart the game.");
        return;
    }
    if (RImplementation.o.dx10_msaa)
    {
        ActivationFailed("MSAA is enabled and cannot be combined with temporal upscaling.",
            "Turn MSAA Off in Video settings and Apply again (or run vid_restart).");
        return;
    }
    Size size;
    const Size display{Device.dwWidth, Device.dwHeight};
    if (UsesFsr())
    {
        // Release NGX as well when switching methods; FSR does not require NVIDIA.
        backend.Shutdown();
        // FSR 3.1 and FSR 4 share the quality-mode ratios.
        if (!fsr3::D3D11Backend::OptimalSize(ps_r_fsr3_quality, display, size))
        {
            ActivationFailed("Could not determine the FSR render resolution.",
                "Select a valid display resolution and FSR quality, then Apply again.");
            return;
        }
#ifdef XR_DX12_BRIDGE
        if (method == Upscaler::FsrDx12)
        {
            if (!fsrDx12Backend.Create(size, display, false /* no HDR10 in this tree */))
            {
                ActivationFailed("AMD FSR (DX12) could not start; see the [FSR] lines in the log.",
                    "Check that the amd_fidelityfx_*.dll files are beside the executable, or select DLSS or FSR 3.");
                return;
            }
        }
        else
#endif
        if (!fsrBackend.Create(HW.pDevice, size, display, false /* no HDR10 in this tree */, LogFsr))
        {
            ActivationFailed(BackendReason("FSR 3 context creation failed."),
                "FSR 3 requires Direct3D feature level 11.1. Check the console/log, then reapply or switch Upscaling Off.");
            return;
        }
    }
    else
    {
        wchar_t wideCache[MAX_PATH] = {};
        if (!NgxCachePath(wideCache))
        {
            LogDlss("Cannot convert the application-data path for NGX", GetLastError());
            ActivationFailed(backendError, "Check the application-data path in fsgame.ltx, then restart the game.");
            return;
        }
        if (!backend.Initialize(HW.pDevice, wideCache, LogDlss))
        {
            dlssSupport = 0;
            ActivationFailed(BackendReason("NVIDIA NGX initialization failed."),
                "Check RTX GPU/driver support and nvngx_dlss.dll beside the executable, then restart the game.");
            return;
        }
        dlssSupport = 1;
        const auto quality = static_cast<Quality>(ps_r_dlss_quality);
        if (!backend.OptimalSize(quality, display, size))
        {
            ActivationFailed(BackendReason("Could not determine the DLSS render resolution."),
                "Select a valid display resolution and DLSS quality, then Apply again.");
            return;
        }
        if (!backend.Create(quality, size, display, false /* no HDR10 in this tree */, static_cast<Preset>(ps_r_dlss_preset)))
        {
            ActivationFailed(BackendReason("DLSS feature creation failed."),
                "Check the console/log and GPU driver/runtime. Reapply Upscaling or switch it Off.");
            return;
        }
    }

    D3D11_TEXTURE2D_DESC depth = {};
    depth.Width = size.width;
    depth.Height = size.height;
    depth.MipLevels = depth.ArraySize = depth.SampleDesc.Count = 1;
    depth.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    HRESULT result = HW.pDevice->CreateTexture2D(&depth, nullptr, &sceneDepth);
    if (SUCCEEDED(result)) result = HW.pDevice->CreateDepthStencilView(sceneDepth.Get(), &view, &sceneDepthView);
    if (FAILED(result))
    {
        (UsesFsr() ? LogFsr : LogDlss)("Scene depth allocation failed; retaining native resolution", result);
        ActivationFailed(backendError, "Check GPU memory/device errors in the console/log, then restart the game.");
        ReleaseTargets();
        return;
    }
    renderSize = size;
    configured = true;
    if (UsesFsr())
        Msg("[%s] Initialized; awaiting first scene evaluation: quality=%u scene=%ux%u display=%ux%u",
            Method(), ps_r_fsr3_quality, size.width, size.height, display.width, display.height);
    else
        Msg("[DLSS] Initialized; awaiting first scene evaluation: quality=%u requested preset=%u scene=%ux%u display=%ux%u",
            ps_r_dlss_quality, ps_r_dlss_preset, size.width, size.height, display.width, display.height);
}

void ReleaseTargets()
{
    inScene = mainView = configured = false;
    jitterIndex = 0;
    haveCamera = false;
    reportedEvaluation = false;
    rasterJitter = {};
    previousJitter = {};
    backend.ReleaseFeature();
    fsrBackend.Destroy();
#ifdef XR_DX12_BRIDGE
    fsrDx12Backend.Destroy();
#endif
    method = Upscaler::Dlss;
    sceneDepthView.Reset();
    sceneDepth.Reset();
    renderSize = {};
}

void ShutdownDevice() { ReleaseTargets(); backend.Shutdown(); dlssSupport = -1; }

bool UpscalerSupported(unsigned id)
{
    switch (id)
    {
    case 0: return true;
    case 1:
    {
        if (dlssSupport >= 0) return dlssSupport == 1;
        if (backend.Initialized()) return (dlssSupport = 1) == 1;
        // NGX exists only for NVIDIA GPUs; RTX is decided by NGX itself.
        wchar_t cache[MAX_PATH] = {};
        if (HW.Caps.id_vendor != 0x10DE || !NgxCachePath(cache)) return (dlssSupport = 0) == 1;
        dlssSupport = backend.Initialize(HW.pDevice, cache, LogProbe) ? 1 : 0;
        // Keep NGX loaded only while DLSS is the selected method.
        if (method != Upscaler::Dlss || !configured) backend.Shutdown();
        Msg("[Upscaler] DLSS support probe: %s", dlssSupport ? "available" : "unavailable");
        return dlssSupport == 1;
    }
    case 2: return HW.pDevice && HW.pDevice->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1;
#ifdef XR_DX12_BRIDGE
    case 3: return HW.PresentationBridge() && HW.PresentationBridge()->FsrRuntimeAvailable() &&
        *HW.PresentationBridge()->FsrUpscalerVersion();
#endif
    default: return false;
    }
}

bool FrameGenerationSupported(unsigned id)
{
#ifdef XR_DX12_BRIDGE
    switch (id)
    {
    case 0: return true;
    case 1:
        switch (StreamlineIntegration::FrameGenerationSupport())
        {
        case StreamlineIntegration::Support::Supported:
        case StreamlineIntegration::Support::RestartRequired: return true;
        case StreamlineIntegration::Support::Unsupported: return false;
        default: return HW.Caps.id_vendor == 0x10DE; // Streamline not started yet this session.
        }
    case 2: return HW.PresentationBridge() && HW.PresentationBridge()->FsrRuntimeAvailable() &&
        *HW.PresentationBridge()->FsrFrameGenerationVersion();
    default: return false;
    }
#else
    return id == 0;
#endif
}

void SupportSummary(char* text, unsigned size)
{
    const char* fsrUpscaler = "0";
    const char* fsrGeneration = "0";
    const char* dlssGeneration = "0";
#ifdef XR_DX12_BRIDGE
    if (auto* bridge = HW.PresentationBridge(); bridge && bridge->FsrRuntimeAvailable())
    {
        if (*bridge->FsrUpscalerVersion()) fsrUpscaler = bridge->FsrUpscalerVersion();
        if (*bridge->FsrFrameGenerationVersion()) fsrGeneration = bridge->FsrFrameGenerationVersion();
    }
    if (FrameGenerationSupported(1))
    {
        // Usable now if Streamline runs, or if it has not started in this process yet.
        auto* bridge = HW.PresentationBridge();
        const bool running = bridge && bridge->Runtime() == PresentRuntime::Streamline && bridge->FrameGenerationReady();
        dlssGeneration = running || StreamlineIntegration::CanInitialize() ? "1" : "restart";
    }
#endif
    xr_sprintf(text, size, "dlss=%u fsr3=%u fsr_dx12=%s fg_dlss=%s fg_fsr=%s",
        unsigned(UpscalerSupported(1)), unsigned(UpscalerSupported(2)), fsrUpscaler, dlssGeneration, fsrGeneration);
}
unsigned RenderWidth() { return renderSize.Valid() ? renderSize.width : Device.dwWidth; }
unsigned RenderHeight() { return renderSize.Valid() ? renderSize.height : Device.dwHeight; }
bool Configured() { return configured; }
bool Active() { return configured && inScene && mainView && BackendReady(); }
Offset RasterJitter() { return rasterJitter; }
Offset PreviousRasterJitter() { return previousJitter; }
ID3DDepthStencilView* SceneDepth() { return inScene && configured ? sceneDepthView.Get() : HW.pBaseZB; }
ID3D11Texture2D* SceneDepthTexture() { return sceneDepth.Get(); }
ID3D11Texture2D* Output()
{
    switch (method)
    {
    case Upscaler::Fsr3: return fsrBackend.Output();
#ifdef XR_DX12_BRIDGE
    case Upscaler::FsrDx12: return fsrDx12Backend.Output();
#endif
    default: return backend.Output();
    }
}

void BeginScene()
{
    inScene = true;
    if (configured)
    {
        const D3D11_VIEWPORT viewport{0, 0, float(RenderWidth()), float(RenderHeight()), 0, 1};
        HW.pContext->RSSetViewports(1, &viewport);
    }
    // Scope frames render spatially at the selected scene resolution. They must
    // neither consume a jitter sample nor overwrite the main camera's history.
    mainView = !Device.m_SecondViewport.IsSVPFrame();
    if (Active())
    {
        if (haveCamera && (lastPosition.distance_to_sqr(Device.vCameraPosition) > 64.0f * 64.0f ||
            lastDirection.dotproduct(Device.vCameraDirection) < 0.25f || _abs(lastFov - Device.fFOV) > 10.0f))
        {
            ResetHistory();
            jitterIndex = 0;
        }
        lastPosition = Device.vCameraPosition;
        lastDirection = Device.vCameraDirection;
        lastFov = Device.fFOV;
        haveCamera = true;
        SSManager.SetMipLODBias(ps_r__tf_Mipbias + log2f(float(RenderWidth()) / Device.dwWidth));
    }
    rasterJitter = Active() ? Jitter(jitterIndex++, renderSize, {Device.dwWidth, Device.dwHeight}) : Offset{};
}

void EndScene(bool invalidateHistory)
{
    if (configured && inScene && mainView) previousJitter = rasterJitter;
    if (configured && inScene) SSManager.SetMipLODBias(ps_r__tf_Mipbias);
    inScene = mainView = false;
    rasterJitter = {};
    if (invalidateHistory) { ResetHistory(); jitterIndex = 0; haveCamera = false; previousJitter = {}; }
}

bool Evaluate(ID3D11Texture2D* color, ID3D11Texture2D* motion, ID3D11Texture2D* reactive)
{
    if (!Active()) return false;
    PROF_EVENT_DYNAMIC(method == Upscaler::Fsr3 ? "Render.Upscaler.Evaluate.FSR3" : method == Upscaler::FsrDx12 ? "Render.Upscaler.Evaluate.FSR_DX12" : "Render.Upscaler.Evaluate.DLSS");
    fsr3::D3D11Backend::Frame frame;
    frame.color = color;
    frame.depth = sceneDepth.Get();
    frame.motion = motion;
    frame.reactive = reactive;
    frame.jitter = rasterJitter;
    frame.deltaMilliseconds = Device.fTimeDelta * 1000.0f;
    frame.frame = Device.dwFrame;
    // Derive the actual main-view near/far/FOV from its projection, including
    // camera overrides, rather than assuming the environment's nominal range.
    frame.cameraNear = -Device.mProject._43 / Device.mProject._33;
    frame.cameraFar = -Device.mProject._43 / (Device.mProject._33 - 1.0f);
    frame.verticalFov = 2.0f * atanf(1.0f / Device.mProject._22);
    bool success;
    switch (method)
    {
    case Upscaler::Fsr3: success = fsrBackend.Evaluate(frame); break;
#ifdef XR_DX12_BRIDGE
    case Upscaler::FsrDx12: success = fsrDx12Backend.Evaluate(frame); break;
#endif
    default: success = backend.Evaluate(frame); break;
    }
    if (success && !reportedEvaluation)
    {
        SetRendererError(nullptr);
        Msg("- [%s] ACTIVE - first scene evaluation succeeded: frame=%u, scene=%ux%u, jitter=(%.4f,%.4f), MV scale=(-%u,-%u)",
            Method(), Device.dwFrame, RenderWidth(), RenderHeight(), rasterJitter.x, rasterJitter.y, RenderWidth(), RenderHeight());
        reportedEvaluation = true;
    }
    else if (!success)
    {
        string128 summary;
        xr_sprintf(summary, "[%s] STOPPED - using spatial upscale without temporal reconstruction", Method());
        const char* reason = BackendReason("Scene evaluation failed.");
        const char* recovery = "Check the console/log. Reapply Upscaling (vid_restart) or switch it Off in Video settings.";
        Msg("! %s: %s", summary, reason);
        Msg("! [%s] %s", Method(), recovery);
        ShowBackendError(Method());
    }
    return success;
}
}
