#include "Fsr3D3D11.h"
#include "UpscalerDiagnostics.h"
#include "../../../../sdk/FSR3/include/FidelityFX/host/backends/dx11/ffx_dx11.h"

namespace fsr3
{
using Microsoft::WRL::ComPtr;
static void LogEvaluationInputs(const D3D11Backend::Frame& frame, dlss::Size render,
    ID3D11Texture2D* output, D3D11Backend::Log log,
    unsigned result = unsigned(E_INVALIDARG))
{
    if (!log) return;
    dlss::LogEvaluationInputs(frame, render, output, log, result);
    char message[256];
    std::snprintf(message, sizeof(message),
        "Evaluation camera: near=%.9g far=%.9g vertical_fov=%.9g; invalid_camera=%u",
        double(frame.cameraNear), double(frame.cameraFar), double(frame.verticalFov),
        unsigned(!std::isfinite(frame.cameraNear) || !std::isfinite(frame.cameraFar) ||
            !std::isfinite(frame.verticalFov) || frame.cameraNear <= 0 || frame.cameraFar <= frame.cameraNear ||
            frame.verticalFov <= 0 || frame.verticalFov >= 3.141593f));
    log(message, result);
}
class ContextScope
{
public:
    ContextScope(ID3D11DeviceContext1* context, ID3DDeviceContextState* state) : m_context(context)
    { m_context->SwapDeviceContextState(state, &m_previous); }
    ~ContextScope()
    {
        m_context->ClearState();
        m_context->SwapDeviceContextState(m_previous.Get(), nullptr);
    }
private:
    ID3D11DeviceContext1* m_context;
    ComPtr<ID3DDeviceContextState> m_previous;
};
static FfxResource Resource(ID3D11Texture2D* texture, const wchar_t* name, bool uav = false)
{
    return ffxGetResourceDX11(texture, GetFfxResourceDescriptionDX11(texture), name,
        uav ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
}
bool D3D11Backend::Check(FfxErrorCode result, const char* stage)
{
    if (result == FFX_OK) return true;
    if (m_log) m_log(stage, unsigned(result));
    return false;
}
bool D3D11Backend::OptimalSize(unsigned quality, dlss::Size display, dlss::Size& render)
{
    render = {};
    if (quality > 4 || !display.Valid()) return false;
    if (ffxFsr3UpscalerGetRenderResolutionFromQualityMode(&render.width, &render.height,
        display.width, display.height, static_cast<FfxFsr3UpscalerQualityMode>(quality)) != FFX_OK) return false;
    return render.Valid() && render.width <= display.width && render.height <= display.height;
}
bool D3D11Backend::Create(ID3D11Device* device, dlss::Size render, dlss::Size display, bool hdr, Log log)
try
{
    Destroy();
    m_log = log;
    if (!device || !render.Valid() ||
        !display.Valid() || render.width > display.width || render.height > display.height)
    {
        if (m_log) m_log("FSR3 creation requires a D3D11 device and valid render/display dimensions", unsigned(E_INVALIDARG));
        return false;
    }
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_1)
    {
        if (m_log) m_log("FSR3 requires D3D feature level 11.1; retaining native resolution", unsigned(DXGI_ERROR_UNSUPPORTED));
        return false;
    }
    m_device = device;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device1));
    if (SUCCEEDED(hr)) hr = immediate.As(&m_context);
    const auto level = device->GetFeatureLevel();
    D3D_FEATURE_LEVEL selected;
    if (SUCCEEDED(hr)) hr = device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device), &selected, &m_state);
    if (FAILED(hr))
    {
        if (m_log) m_log("FSR3 private D3D11 context state", hr);
        Destroy(); return false;
    }
    const size_t bytes = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
    // The community backend does not implement pipeline samplers. Bind the
    // two clamp samplers declared by FSR3's createPipelineStates explicitly.
    for (unsigned i = 0; i < 2; ++i)
    {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = i ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxAnisotropy = 1;
        hr = device->CreateSamplerState(&sampler, &m_samplers[i]);
        if (FAILED(hr)) { if (m_log) m_log("FSR3 sampler allocation", hr); Destroy(); return false; }
    }
    m_scratch.assign((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
    FfxFsr3UpscalerContextDescription desc{};
    if (!Check(ffxGetInterfaceDX11(&desc.backendInterface, ffxGetDeviceDX11(device),
        m_scratch.data(), bytes, FFX_FSR3UPSCALER_CONTEXT_COUNT), "FSR3 DX11 interface"))
    { Destroy(); return false; }
    desc.maxRenderSize = {render.width, render.height};
    desc.maxUpscaleSize = {display.width, display.height};
    // Color at this integration point is LDR unless the engine HDR10 path is on.
    if (hdr) desc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
    m_fsr = std::make_unique<FfxFsr3UpscalerContext>();
    FfxErrorCode created;
    m_created = true;
    {
        ContextScope state(m_context.Get(), m_state.Get());
        created = ffxFsr3UpscalerContextCreate(m_fsr.get(), &desc);
    }
    // The SDK initializes the context before allocating pipelines/resources.
    // Destroy also unwinds a partially constructed context on allocation failure.
    if (!Check(created, "FSR3 context creation")) { Destroy(); return false; }

    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    if (!Check(ffxFsr3UpscalerGetSharedResourceDescriptions(m_fsr.get(), &shared), "FSR3 shared descriptions"))
    { Destroy(); return false; }
    const FfxCreateResourceDescription descriptions[] = {shared.dilatedDepth, shared.dilatedMotionVectors, shared.reconstructedPrevNearestDepth};
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto& resource = descriptions[i].resourceDescription;
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = resource.width; texture.Height = resource.height;
        texture.MipLevels = resource.mipCount; texture.ArraySize = std::max(1u, resource.depth);
        texture.SampleDesc.Count = 1;
        switch (resource.format)
        {
        case FFX_SURFACE_FORMAT_R32_FLOAT: texture.Format = DXGI_FORMAT_R32_FLOAT; break;
        case FFX_SURFACE_FORMAT_R16G16_FLOAT: texture.Format = DXGI_FORMAT_R16G16_FLOAT; break;
        case FFX_SURFACE_FORMAT_R32_UINT: texture.Format = DXGI_FORMAT_R32_UINT; break;
        default: if (m_log) m_log("Unsupported FSR3 shared format", unsigned(resource.format)); Destroy(); return false;
        }
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (resource.usage & FFX_RESOURCE_USAGE_RENDERTARGET) texture.BindFlags |= D3D11_BIND_RENDER_TARGET;
        hr = device->CreateTexture2D(&texture, nullptr, &m_shared[i]);
        if (FAILED(hr)) { if (m_log) m_log("FSR3 shared texture allocation", hr); Destroy(); return false; }
    }
    D3D11_TEXTURE2D_DESC output{};
    output.Width = display.width; output.Height = display.height;
    output.MipLevels = output.ArraySize = output.SampleDesc.Count = 1;
    output.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    output.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    hr = device->CreateTexture2D(&output, nullptr, &m_output);
    if (FAILED(hr)) { if (m_log) m_log("FSR3 output allocation", hr); Destroy(); return false; }
    m_render = render; m_display = display;
    return true;
}
catch (HRESULT result)
{
    if (m_log) m_log("FSR3 creation failed; retaining native resolution", unsigned(result));
    Destroy();
    return false;
}
catch (...)
{
    // The imported DX11 SDK throws on failed D3D allocations. Do not let that
    // escape into the engine (e.g. VRAM exhaustion during a mode switch).
    if (m_log) m_log("FSR3 creation exception; retaining native resolution", unsigned(E_FAIL));
    Destroy();
    return false;
}
bool D3D11Backend::Validate(const Frame& frame) const
{
    if (!frame.color || !frame.depth || !frame.motion || frame.color == m_output.Get() ||
        !std::isfinite(frame.jitter.x) || !std::isfinite(frame.jitter.y) ||
        !std::isfinite(frame.deltaMilliseconds) || frame.deltaMilliseconds < 0 ||
        !std::isfinite(frame.cameraNear) || !std::isfinite(frame.cameraFar) ||
        !std::isfinite(frame.verticalFov) || frame.cameraNear <= 0 || frame.cameraFar <= frame.cameraNear ||
        frame.verticalFov <= 0 || frame.verticalFov >= 3.141593f) return false;
    // Older ReShade versions proxy the device but resources report the original.
    // Compare resource devices consistently, using our own output as the reference.
    if (!m_output) return false;
    ComPtr<ID3D11Device> resourceDevice;
    m_output->GetDevice(&resourceDevice);
    for (auto* input : {frame.color, frame.depth, frame.motion})
    {
        D3D11_TEXTURE2D_DESC desc;
        input->GetDesc(&desc);
        ComPtr<ID3D11Device> device;
        input->GetDevice(&device);
        if (device.Get() != resourceDevice.Get() || desc.Width != m_render.width || desc.Height != m_render.height ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return false;
    }
    D3D11_TEXTURE2D_DESC depth, motion;
    frame.depth->GetDesc(&depth); frame.motion->GetDesc(&motion);
    if (depth.Format != DXGI_FORMAT_R32_FLOAT && depth.Format != DXGI_FORMAT_R32_TYPELESS &&
        depth.Format != DXGI_FORMAT_R24G8_TYPELESS && depth.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS) return false;
    return motion.Format == DXGI_FORMAT_R16G16_FLOAT || motion.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        motion.Format == DXGI_FORMAT_R32G32_FLOAT;
}
bool D3D11Backend::Evaluate(const Frame& frame)
try
{
    if (!Ready()) return false;
    if (!Validate(frame))
    {
        LogEvaluationInputs(frame, m_render, m_output.Get(), m_log);
        if (m_log) m_log("Invalid FSR3 inputs; disabling until vid_restart", unsigned(E_INVALIDARG));
        m_failed = true; m_history.Invalidate(); return false;
    }
    FfxFsr3UpscalerDispatchDescription dispatch{};
    dispatch.commandList = ffxGetCommandListDX11(m_context.Get());
    dispatch.color = Resource(frame.color, L"FSR3 color");
    dispatch.depth = Resource(frame.depth, L"FSR3 depth");
    dispatch.motionVectors = Resource(frame.motion, L"FSR3 motion");
    dispatch.dilatedDepth = Resource(m_shared[0].Get(), L"FSR3 dilated depth", true);
    dispatch.dilatedMotionVectors = Resource(m_shared[1].Get(), L"FSR3 dilated motion", true);
    dispatch.reconstructedPrevNearestDepth = Resource(m_shared[2].Get(), L"FSR3 reconstructed depth", true);
    dispatch.output = Resource(m_output.Get(), L"FSR3 output", true);
    dispatch.jitterOffset = {frame.jitter.x, frame.jitter.y};
    // SSS stores currentUV - previousUV, unlike OGSR's clip-space vectors.
    const auto scale = dlss::MotionScale(m_render);
    dispatch.motionVectorScale = {scale.x, scale.y};
    dispatch.renderSize = {m_render.width, m_render.height};
    dispatch.upscaleSize = {m_display.width, m_display.height};
    dispatch.frameTimeDelta = std::max(frame.deltaMilliseconds, 0.1f);
    dispatch.preExposure = 1.0f;
    dispatch.cameraNear = frame.cameraNear; dispatch.cameraFar = frame.cameraFar;
    dispatch.cameraFovAngleVertical = frame.verticalFov;
    dispatch.viewSpaceToMetersFactor = 1.0f;
    dispatch.reset = m_history.Begin(frame.frame, frame.cameraCut);
    // Same post-upscale CAS as DLSS; do not apply RCAS a second time.
    // Reactive/composition masks are optional, currently absent as in OGSR.
    ContextScope state(m_context.Get(), m_state.Get());
    ID3D11SamplerState* samplers[] = {m_samplers[0].Get(), m_samplers[1].Get()};
    m_context->CSSetSamplers(0, 2, samplers);
    const auto result = ffxFsr3UpscalerContextDispatch(m_fsr.get(), &dispatch);
    if (result != FFX_OK)
    {
        LogEvaluationInputs(frame, m_render, m_output.Get(), m_log, unsigned(result));
        Check(result, "FSR3 evaluation failed; disabling until vid_restart");
        m_failed = true; m_history.Invalidate(); return false;
    }
    m_history.Commit(frame.frame);
    return true;
}
catch (HRESULT result)
{
    LogEvaluationInputs(frame, m_render, m_output.Get(), m_log, unsigned(result));
    if (m_log) m_log("FSR3 dispatch failed; disabling until vid_restart", unsigned(result));
    m_failed = true;
    m_history.Invalidate();
    return false;
}
catch (...)
{
    LogEvaluationInputs(frame, m_render, m_output.Get(), m_log, unsigned(E_FAIL));
    if (m_log) m_log("FSR3 dispatch exception; disabling until vid_restart", unsigned(E_FAIL));
    m_failed = true;
    m_history.Invalidate();
    return false;
}
void D3D11Backend::Destroy()
{
    if (m_created) Check(ffxFsr3UpscalerContextDestroy(m_fsr.get()), "FSR3 context release");
    m_created = m_failed = false;
    m_fsr.reset();
    m_scratch.clear();
    for (auto& resource : m_shared) resource.Reset();
    for (auto& sampler : m_samplers) sampler.Reset();
    m_output.Reset(); m_state.Reset(); m_context.Reset(); m_device.Reset();
    m_render = m_display = {};
    m_history.Invalidate();
}
}
