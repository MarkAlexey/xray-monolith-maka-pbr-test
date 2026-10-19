#include "DlssD3D11.h"
#include "UpscalerDiagnostics.h"
#include "../../../../sdk/DLSS/include/nvsdk_ngx.h"
#include "../../../../sdk/DLSS/include/nvsdk_ngx_helpers.h"

namespace dlss
{
using Microsoft::WRL::ComPtr;

static NVSDK_NGX_PerfQuality_Value NgxQuality(Quality quality)
{
    switch (quality)
    {
    case Quality::Quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case Quality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case Quality::Performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case Quality::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default: return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
}

D3D11Backend::~D3D11Backend() { Shutdown(); }

bool D3D11Backend::Check(unsigned result, const char* stage)
{
    if (NVSDK_NGX_SUCCEED(static_cast<NVSDK_NGX_Result>(result))) return true;
    if (m_log)
    {
        char message[512];
        const wchar_t* description = GetNGXResultAsString(static_cast<NVSDK_NGX_Result>(result));
        std::snprintf(message, sizeof(message), "%s: %ls", stage, description ? description : L"Unknown NGX error");
        m_log(message, result);
    }
    return false;
}

bool D3D11Backend::Initialize(ID3D11Device* device, const wchar_t* cachePath, Log log)
{
    if (m_initialized && m_device.Get() == device) return m_parameters != nullptr;
    Shutdown();
    m_log = log;
    if (!device || !cachePath || !*cachePath)
    {
        if (m_log) m_log("DLSS initialization requires a D3D11 device and a valid cache path", unsigned(E_INVALIDARG));
        return false;
    }

    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) || FAILED(immediate.As(&m_context)))
    {
        if (m_log) m_log("D3D11.1 context-state support is required", unsigned(E_NOINTERFACE));
        Shutdown();
        return false;
    }
    // NGX can change pipeline bindings. Swap a private context state around each
    // evaluation so the engine's cached state remains accurate, including UAVs.
    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    D3D_FEATURE_LEVEL selected;
    const HRESULT hr = device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device), &selected, &m_state);
    if (FAILED(hr))
    {
        if (m_log) m_log("CreateDeviceContextState", hr);
        Shutdown();
        return false;
    }
    m_device = device;
    // Stable project identifier for this integration, not a borrowed NVIDIA app ID.
    const auto result = NVSDK_NGX_D3D11_Init_with_ProjectID(
        "6f27fb13-3c63-4b18-8f66-503897a6aabe", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "xray-monolith", cachePath, device);
    if (!Check(result, "NGX D3D11 initialization")) { Shutdown(); return false; }
    m_initialized = true;
    if (!Check(NVSDK_NGX_D3D11_GetCapabilityParameters(&m_parameters), "NGX capabilities"))
    {
        Shutdown();
        return false;
    }
    int available = 0;
    if (!Check(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available), "DLSS availability") || !available)
    {
        if (m_log) m_log("DLSS is unavailable on this GPU/driver/runtime", 0);
        Shutdown();
        return false;
    }
    return true;
}

bool D3D11Backend::OptimalSize(Quality quality, Size display, Size& render)
{
    render = {};
    if (!m_parameters || quality == Quality::Off || quality > Quality::UltraPerformance || !display.Valid())
    {
        if (m_log) m_log("DLSS optimal dimensions: missing capabilities, invalid quality or display size", unsigned(E_INVALIDARG));
        return false;
    }
    if (quality == Quality::DLAA) { render = display; return true; }
    Size optimal, minimum, maximum;
    float sharpness;
    if (!Check(NGX_DLSS_GET_OPTIMAL_SETTINGS(m_parameters, display.width, display.height, NgxQuality(quality),
        &optimal.width, &optimal.height, &maximum.width, &maximum.height,
        &minimum.width, &minimum.height, &sharpness), "DLSS optimal dimensions")) return false;
    if (!optimal.Valid() || optimal.width > display.width || optimal.height > display.height)
    {
        if (m_log) m_log("NGX returned invalid DLSS render dimensions", unsigned(E_INVALIDARG));
        return false;
    }
    render = optimal;
    return true;
}

bool D3D11Backend::Create(Quality quality, Size render, Size display, bool hdr, Preset preset)
{
    ReleaseFeature();
    if (!m_parameters || !ValidPreset(preset) || quality == Quality::Off || quality > Quality::UltraPerformance ||
        !render.Valid() || !display.Valid() || render.width > display.width || render.height > display.height)
    {
        if (m_log) m_log("DLSS feature creation: invalid capabilities, quality, preset or dimensions", unsigned(E_INVALIDARG));
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = display.width;
    desc.Height = display.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_output);
    if (SUCCEEDED(hr)) hr = m_device->CreateShaderResourceView(m_output.Get(), nullptr, &m_outputView);
    if (FAILED(hr))
    {
        if (m_log) m_log("DLSS output allocation", hr);
        ReleaseFeature();
        return false;
    }

    // Parameters survive vid_restart; always overwrite every quality hint so
    // choosing Default cannot inherit the previous feature's explicit preset.
    for (const auto* hint : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
        m_parameters->Set(hint, static_cast<unsigned>(preset));

    NVSDK_NGX_DLSS_Create_Params create = {};
    create.Feature.InWidth = render.width;
    create.Feature.InHeight = render.height;
    create.Feature.InTargetWidth = display.width;
    create.Feature.InTargetHeight = display.height;
    create.Feature.InPerfQualityValue = NgxQuality(quality);
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (hdr) create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    ComPtr<ID3DDeviceContextState> previous;
    m_context->SwapDeviceContextState(m_state.Get(), &previous);
    const auto result = NGX_D3D11_CREATE_DLSS_EXT(m_context.Get(), &m_feature, m_parameters, &create);
    m_context->ClearState(); // Do not retain references to engine resources in our state.
    m_context->SwapDeviceContextState(previous.Get(), nullptr);
    if (!Check(result, "DLSS feature creation")) { ReleaseFeature(); return false; }
    m_render = render;
    m_display = display;
    return true;
}

bool D3D11Backend::Validate(const Frame& frame) const
{
    if (!frame.color || !frame.depth || !frame.motion || frame.color == m_output.Get() ||
        !std::isfinite(frame.jitter.x) || !std::isfinite(frame.jitter.y) ||
        !std::isfinite(frame.deltaMilliseconds) || frame.deltaMilliseconds < 0) return false;
    // Older ReShade versions proxy the device but resources report the original.
    // Compare resource devices consistently, using our own output as the reference.
    if (!m_output) return false;
    ComPtr<ID3D11Device> resourceDevice;
    m_output->GetDevice(&resourceDevice);
    for (auto* input : {frame.color, frame.depth, frame.motion})
    {
        D3D11_TEXTURE2D_DESC desc;
        input->GetDesc(&desc);
        if (desc.Width != m_render.width || desc.Height != m_render.height || desc.SampleDesc.Count != 1 ||
            desc.ArraySize != 1 || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return false;
        ComPtr<ID3D11Device> device;
        input->GetDevice(&device);
        if (device.Get() != resourceDevice.Get()) return false;
    }
    D3D11_TEXTURE2D_DESC depth, motion;
    frame.depth->GetDesc(&depth);
    frame.motion->GetDesc(&motion);
    if (depth.Format != DXGI_FORMAT_R32_FLOAT && depth.Format != DXGI_FORMAT_R32_TYPELESS &&
        depth.Format != DXGI_FORMAT_R24G8_TYPELESS && depth.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS) return false;
    return motion.Format == DXGI_FORMAT_R16G16_FLOAT || motion.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        motion.Format == DXGI_FORMAT_R32G32_FLOAT;
}

bool D3D11Backend::Evaluate(const Frame& frame)
{
    if (!Ready()) return false;
    if (!Validate(frame))
    {
        LogEvaluationInputs(frame, m_render, m_output.Get(), m_log);
        if (m_log) m_log("Invalid DLSS input resources; disabling until render-target reset", unsigned(E_INVALIDARG));
        m_failed = true;
        m_history.Invalidate();
        return false;
    }
    NVSDK_NGX_D3D11_DLSS_Eval_Params eval = {};
    eval.Feature.pInColor = frame.color;
    eval.Feature.pInOutput = m_output.Get();
    eval.pInDepth = frame.depth;
    eval.pInMotionVectors = frame.motion;
    eval.InJitterOffsetX = frame.jitter.x;
    eval.InJitterOffsetY = frame.jitter.y;
    const Offset motionScale = MotionScale(m_render);
    eval.InMVScaleX = motionScale.x;
    eval.InMVScaleY = motionScale.y;
    eval.InRenderSubrectDimensions = {m_render.width, m_render.height};
    eval.InReset = m_history.Begin(frame.frame, frame.cameraCut);
    eval.InFrameTimeDeltaInMsec = frame.deltaMilliseconds;
    eval.InPreExposure = eval.InExposureScale = 1.0f;

    ComPtr<ID3DDeviceContextState> previous;
    m_context->SwapDeviceContextState(m_state.Get(), &previous);
    const auto result = NGX_D3D11_EVALUATE_DLSS_EXT(m_context.Get(), m_feature, m_parameters, &eval);
    m_context->ClearState();
    m_context->SwapDeviceContextState(previous.Get(), nullptr);
    if (NVSDK_NGX_FAILED(result))
    {
        LogEvaluationInputs(frame, m_render, m_output.Get(), m_log, unsigned(result));
        // Keep the SDK error as the final diagnostic used by the STOPPED log.
        Check(result, "DLSS evaluation failed; disabling until render-target reset");
        m_failed = true;
        m_history.Invalidate();
        return false;
    }
    m_history.Commit(frame.frame);
    return true;
}

void D3D11Backend::ReleaseFeature()
{
    if (m_feature) Check(NVSDK_NGX_D3D11_ReleaseFeature(m_feature), "DLSS feature release");
    m_feature = nullptr;
    m_outputView.Reset();
    m_output.Reset();
    m_render = m_display = {};
    m_history.Invalidate();
    m_failed = false;
}

void D3D11Backend::Shutdown()
{
    ReleaseFeature();
    if (m_parameters) Check(NVSDK_NGX_D3D11_DestroyParameters(m_parameters), "NGX parameter release");
    m_parameters = nullptr;
    if (m_initialized) Check(NVSDK_NGX_D3D11_Shutdown1(m_device.Get()), "NGX shutdown");
    m_initialized = false;
    m_state.Reset();
    m_context.Reset();
    m_device.Reset();
}
}
