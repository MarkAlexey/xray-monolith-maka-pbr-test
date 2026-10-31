// Adapted from OGSR revision 6443f8426; credits: OGSR team and xrSimpAI.
// Deliberately independent of the engine/PCH so the actual interop implementation
// can also be exercised by a small GPU smoke test.
#include "D3D12PresentBridge.h"
#include "FfxIntegration.h"
#include <d3d12sdklayers.h>
#include <limits>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#pragma comment(lib, "d3dcompiler.lib")

namespace
{
struct SharedHandle
{
    HANDLE value{};
    ~SharedHandle() { if (value) CloseHandle(value); }
};

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

// Shared DX12 textures need a typed format; DX11 copies between typeless and typed
// members of the same format group.
DXGI_FORMAT TypedColorFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return format;
    }
}

FfxApiResource FfxShared(ID3D12Resource* resource)
{
    // Bridge textures are simultaneous-access and decay to COMMON between submissions.
    return ffxApiGetResourceDX12(resource, FFX_API_RESOURCE_STATE_COMMON);
}
}

// Preserve the first failure so shutdown/rollback cannot hide its cause.
HRESULT D3D12PresentBridge::Check(HRESULT result, const char* stage)
{
    if (FAILED(result))
    {
        if (m_logger)
        {
            char message[512]{};
            snprintf(message, sizeof(message), "[DX12 bridge] Failed at %s (0x%08lX)", stage, static_cast<unsigned long>(result));
            m_logger(message);
        }
        if (m_failureStage == nullptr) m_failureStage = stage;
        if (result == HRESULT_FROM_WIN32(WAIT_TIMEOUT) || result == DXGI_ERROR_DEVICE_REMOVED ||
            result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DEVICE_HUNG ||
            result == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
            ReportStall(stage, result);
    }
    return result;
}

void D3D12PresentBridge::ReportStall(const char* stage, HRESULT result)
{
    if (m_stallReported || !m_logger) return;
    m_stallReported = true;
    auto completed = [](ID3D12Fence* fence) { return fence ? static_cast<unsigned long long>(fence->GetCompletedValue()) : 0ull; };
    char message[512]{};
    snprintf(message, sizeof(message),
        "[DX12 bridge] Stall report at %s (0x%08lX): runtime=%s generating=%u serial=%llu "
        "ready(DX11->DX12) %llu/%llu finished %llu retired %llu/%llu; removed reason DX12=0x%08lX DX11=0x%08lX",
        stage, static_cast<unsigned long>(result), RuntimeName(), unsigned(m_lastGenerated),
        static_cast<unsigned long long>(m_serial), completed(m_ready12.Get()), static_cast<unsigned long long>(m_lastReady),
        completed(m_finished12.Get()), completed(m_retired12.Get()), static_cast<unsigned long long>(m_lastRetired),
        static_cast<unsigned long>(m_device12 ? m_device12->GetDeviceRemovedReason() : S_OK),
        static_cast<unsigned long>(m_device11 ? m_device11->GetDeviceRemovedReason() : S_OK));
    m_logger(message);
    // Process-wide use of this GPU's dedicated memory (DX11 rendering and DX12
    // presentation together) against the budget Windows grants the game.
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
    if (m_adapter && SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
        SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared)))
    {
        constexpr double MiB = 1024.0 * 1024.0;
        snprintf(message, sizeof(message),
            "[DX12 bridge] Stall report VRAM: %.0f/%.0f MiB used/budget (dedicated); %.0f/%.0f MiB in system memory",
            local.CurrentUsage / MiB, local.Budget / MiB, shared.CurrentUsage / MiB, shared.Budget / MiB);
        m_logger(message);
    }
    for (size_t i = 0; i < m_slots.size(); ++i)
    {
        const auto& slot = m_slots[i];
        snprintf(message, sizeof(message), "[DX12 bridge] Stall report slot %zu%s: finished %llu retired %llu DLSS-G inputs %llu/%llu",
            i, i == m_currentSlot ? " (current)" : "", static_cast<unsigned long long>(slot.finished),
            static_cast<unsigned long long>(slot.retired), completed(slot.inputFence.Get()),
            static_cast<unsigned long long>(slot.inputValue));
        m_logger(message);
    }
}

#define BRIDGE_CHECK(expression) do { const HRESULT bridgeResult = Check((expression), #expression); if (FAILED(bridgeResult)) return bridgeResult; } while (false)

D3D12PresentBridge::D3D12PresentBridge() : m_ffx(std::make_unique<FfxApi>()) {}
D3D12PresentBridge::~D3D12PresentBridge() { Shutdown(); }

const char* D3D12PresentBridge::RuntimeName() const
{
    switch (m_runtimeKind)
    {
    case PresentRuntime::Streamline: return "Streamline (Reflex, DLSS frame generation)";
    case PresentRuntime::Fsr: return "AMD FSR frame generation swapchain";
    default: return "plain DX12";
    }
}

bool D3D12PresentBridge::FrameGenerationReady() const
{
    if (m_fgFailed) return false;
    if (m_runtimeKind == PresentRuntime::Streamline) return m_streamline.IsReady();
    if (m_runtimeKind == PresentRuntime::Fsr) return m_fsrFrameGeneration != nullptr;
    return false;
}

HRESULT D3D12PresentBridge::Initialize(IDXGIFactory2* factory, IDXGIAdapter* adapter, ID3D11Device* device11,
    ID3D11DeviceContext* context11, HWND window, const DXGI_SWAP_CHAIN_DESC1& desc, bool debug,
    PresentRuntime runtime, Logger logger, bool profiling)
{
    if (m_device12 || m_runtime) return E_UNEXPECTED;
    m_failureStage = nullptr;
    m_logger = logger;
    m_profiling = profiling;
    m_statistics = {};
    m_runtimeKind = runtime;
    if (!factory || !adapter || !device11 || !context11 || !window || desc.BufferCount < 2 || !desc.Width || !desc.Height || desc.SampleDesc.Count != 1 || desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        return Check(E_INVALIDARG, "Initialize arguments");
    if (context11->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return Check(E_INVALIDARG, "Immediate D3D11 context required");
    adapter->QueryInterface(IID_PPV_ARGS(&m_adapter)); // optional: VRAM figures in ReportStall

    // Verify identity, rather than silently attempting cross-adapter sharing.
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> renderAdapter;
    BRIDGE_CHECK(device11->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));
    BRIDGE_CHECK(dxgiDevice->GetAdapter(&renderAdapter));
    DXGI_ADAPTER_DESC renderDesc{}, bridgeDesc{};
    BRIDGE_CHECK(renderAdapter->GetDesc(&renderDesc));
    BRIDGE_CHECK(adapter->GetDesc(&bridgeDesc));
    if (renderDesc.AdapterLuid.LowPart != bridgeDesc.AdapterLuid.LowPart || renderDesc.AdapterLuid.HighPart != bridgeDesc.AdapterLuid.HighPart)
        return Check(E_INVALIDARG, "D3D11/D3D12 adapter LUID mismatch");

    if (runtime == PresentRuntime::Streamline)
    {
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, UINT(std::size(executable)));
        if (!length || length >= std::size(executable)) return Check(E_FAIL, "Executable path");
        std::wstring directory(executable);
        directory.resize(directory.find_last_of(L"\\/"));
        directory += L"\\streamline";
        if (!m_streamline.Initialize(directory.c_str(), logger) || !m_streamline.CheckAdapter(bridgeDesc.AdapterLuid))
            return Check(E_FAIL, m_streamline.FailureStage());
    }
    else if (runtime == PresentRuntime::Fsr && !m_ffx->Load(logger, profiling))
        return Check(E_FAIL, m_ffx->FailureStage());

    BRIDGE_CHECK(device11->QueryInterface(IID_PPV_ARGS(&m_device11)));
    BRIDGE_CHECK(context11->QueryInterface(IID_PPV_ARGS(&m_context11)));
    // No D3D12 import dependency for users staying on the legacy presentation path.
    m_runtime = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m_runtime) return Check(HRESULT_FROM_WIN32(GetLastError()), "Load system d3d12.dll");
    const auto createDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(m_runtime, "D3D12CreateDevice"));
    if (!createDevice) return Check(E_NOINTERFACE, "D3D12CreateDevice export");
    if (debug)
    {
        const auto getDebug = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(GetProcAddress(m_runtime, "D3D12GetDebugInterface"));
        if (!getDebug) return Check(E_NOINTERFACE, "D3D12GetDebugInterface export");
        ComPtr<ID3D12Debug> layer;
        BRIDGE_CHECK(getDebug(IID_PPV_ARGS(&layer)));
        layer->EnableDebugLayer();
    }
    BRIDGE_CHECK(createDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device12)));
    LogDeviceCapabilities();
    if (runtime == PresentRuntime::Streamline)
    {
        if (!m_streamline.SetDevice(m_device12.Get())) return Check(E_FAIL, m_streamline.FailureStage());
        m_proxyDevice = m_device12;
        auto* proxy = m_proxyDevice.Detach();
        const bool upgraded = m_streamline.Upgrade(reinterpret_cast<void**>(&proxy));
        m_proxyDevice.Attach(proxy);
        if (!upgraded) return Check(E_FAIL, m_streamline.FailureStage());
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    BRIDGE_CHECK((m_proxyDevice ? m_proxyDevice.Get() : m_device12.Get())->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)));
    m_queue->SetName(L"X-Ray D3D11-D3D12 presentation bridge");
    BRIDGE_CHECK(CreateSharedFence(m_ready12, m_ready11));
    BRIDGE_CHECK(CreateSharedFence(m_finished12, m_finished11));
    if (runtime != PresentRuntime::Plain)
    {
        // Keep input retirement ordered on the presentation queue. With either FG
        // runtime that queue is ordered behind frame generation of the same frame.
        BRIDGE_CHECK(CreateSharedFence(m_retired12, m_retired11));
    }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) return Check(HRESULT_FROM_WIN32(GetLastError()), "Create fence event");

    m_desc = desc;
    if (runtime != PresentRuntime::Plain) m_desc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
    fullscreen.Windowed = TRUE;
    ComPtr<IDXGISwapChain1> swapChain;
    if (runtime == PresentRuntime::Fsr)
        BRIDGE_CHECK(CreateFsrSwapChain(factory, window, fullscreen, swapChain));
    else
    {
        ComPtr<IDXGIFactory2> presentationFactory = factory;
        if (runtime == PresentRuntime::Streamline)
        {
            auto* proxy = presentationFactory.Detach();
            const bool upgraded = m_streamline.Upgrade(reinterpret_cast<void**>(&proxy));
            presentationFactory.Attach(proxy);
            if (!upgraded) return Check(E_FAIL, m_streamline.FailureStage());
        }
        BRIDGE_CHECK(presentationFactory->CreateSwapChainForHwnd(m_queue.Get(), window, &m_desc, &fullscreen, nullptr, &swapChain));
    }
    BRIDGE_CHECK(swapChain.As(&m_swapChain));
    BRIDGE_CHECK(CreateSurfaces());
    if (runtime == PresentRuntime::Fsr) BRIDGE_CHECK(CreateFsrFrameGeneration());
    return S_OK;
}

void D3D12PresentBridge::LogDeviceCapabilities()
{
    if (!m_logger || !m_device12) return;
    // Walk down from the newest model this code knows; unknown values are rejected.
    D3D12_FEATURE_DATA_SHADER_MODEL model{};
    unsigned highest = 0;
    for (unsigned value = 0x69; value >= 0x60; --value)
    {
        model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(value);
        if (SUCCEEDED(m_device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))))
        {
            highest = unsigned(model.HighestShaderModel);
            break;
        }
    }
    char core[MAX_PATH]{};
    if (const HMODULE module = GetModuleHandleW(L"D3D12Core.dll"))
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, core, int(sizeof(core) - 1), nullptr, nullptr);
    }
    char message[512]{};
    snprintf(message, sizeof(message), "[DX12 bridge] D3D12 core: %s; highest shader model %u.%u (FSR 4 needs 6.6).",
        core[0] ? core : "unknown", highest >> 4, highest & 15);
    m_logger(message);
}

HRESULT D3D12PresentBridge::CreateFsrSwapChain(IDXGIFactory2* factory, HWND window,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen, ComPtr<IDXGISwapChain1>& swapChain)
{
    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
    version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = fullscreen;
    IDXGISwapChain4* created{};
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    create.header.pNext = &version.header;
    create.swapchain = &created;
    create.hwnd = window;
    create.desc = &m_desc;
    create.fullscreenDesc = &fullscreenDesc;
    create.dxgiFactory = factory;
    create.gameQueue = m_queue.Get();
    const auto result = m_ffx->CreateContext(&m_fsrSwapChainContext, &create.header);
    if (result != FFX_API_RETURN_OK || !created)
    {
        if (created) created->Release();
        m_fsrSwapChainContext = nullptr;
        char message[160]{};
        snprintf(message, sizeof(message), "! [FSR FG] Swapchain creation failed (FFX return %u).", unsigned(result));
        if (m_logger) m_logger(message);
        return Check(E_FAIL, "Create AMD FSR frame-generation swapchain");
    }
    swapChain.Attach(created);
    return S_OK;
}

HRESULT D3D12PresentBridge::CreateFsrFrameGeneration()
{
    DestroyFsrFrameGeneration();
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = m_device12.Get();
    ffxCreateContextDescFrameGenerationVersion version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    version.header.pNext = &backend.header;
    version.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGeneration create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.header.pNext = &version.header;
    // HDR10 output is PQ-encoded R10G10B10A2; SDR output is R8G8B8A8.
    create.flags = m_desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0;
    create.displaySize = {m_desc.Width, m_desc.Height};
    create.maxRenderSize = {m_desc.Width, m_desc.Height};
    create.backBufferFormat = ffxApiGetSurfaceFormatDX12(m_desc.Format);
    const auto result = m_ffx->CreateContext(&m_fsrFrameGeneration, &create.header);
    if (result != FFX_API_RETURN_OK)
    {
        m_fsrFrameGeneration = nullptr;
        char message[160]{};
        snprintf(message, sizeof(message), "! [FSR FG] Frame generation context failed (FFX return %u).", unsigned(result));
        if (m_logger) m_logger(message);
        return Check(E_FAIL, "Create AMD FSR frame-generation context");
    }
    const std::string version_name = m_ffx->ProviderVersion(&m_fsrFrameGeneration);
    if (m_logger && version_name != m_fsrFgVersion)
    {
        char message[256]{};
        snprintf(message, sizeof(message), "[FSR FG] Frame generation provider %s at %ux%u (4.x requires Windows 11 and Radeon RX 9000; other systems use the 3.1 runtime).",
            version_name.c_str(), m_desc.Width, m_desc.Height);
        m_logger(message);
    }
    m_fsrFgVersion = version_name;
    m_resetHistory = true;
    return S_OK;
}

void D3D12PresentBridge::DestroyFsrFrameGeneration()
{
    if (!m_fsrFrameGeneration) return;
    // Disabling waits for the proxy swapchain's interpolation/UI work that references
    // the context's resources. Required before destruction.
    ffxConfigureDescFrameGeneration off{};
    off.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    off.swapChain = m_swapChain.Get();
    off.frameGenerationEnabled = false;
    off.frameID = m_fsrFrameId;
    m_ffx->Configure(&m_fsrFrameGeneration, &off.header);
    m_ffx->DestroyContext(&m_fsrFrameGeneration);
    m_fsrFrameGeneration = nullptr;
}

uint32_t D3D12PresentBridge::FsrGenerateCallback(ffxDispatchDescFrameGeneration* params, void* user)
{
    auto* bridge = static_cast<D3D12PresentBridge*>(user);
    return bridge->m_ffx->Dispatch(&bridge->m_fsrFrameGeneration, &params->header);
}

HRESULT D3D12PresentBridge::ConfigureFsr(bool generate, Slot& slot)
{
    // Required call order: configure, prepare, then generation from Present.
    ffxConfigureDescFrameGeneration config{};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = m_swapChain.Get();
    config.frameGenerationCallback = &D3D12PresentBridge::FsrGenerateCallback;
    config.frameGenerationCallbackUserContext = this;
    config.frameGenerationEnabled = generate;
    // Synchronous generation on the presentation queue: the existing retirement
    // signal after Present then also covers the HUD-less and prepare inputs.
    config.allowAsyncWorkloads = false;
    if (generate) config.HUDLessColor = FfxShared(slot.inputs12[2].Get());
    config.generationRect = {0, 0, int32_t(m_desc.Width), int32_t(m_desc.Height)};
    config.frameID = m_fsrFrameId;
    if (m_ffx->Configure(&m_fsrFrameGeneration, &config.header) != FFX_API_RETURN_OK) return E_FAIL;
    if (!generate) return S_OK;

    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = m_fsrFrameId;
    prepare.commandList = slot.commands.Get();
    prepare.renderSize = {m_inputWidth, m_inputHeight};
    prepare.jitterOffset = {m_inputs.jitterX, m_inputs.jitterY};
    // FSR wants current->previous pixels; inputs describe the UV conversion.
    prepare.motionVectorScale = {m_inputs.motionToUvX * float(m_inputWidth), m_inputs.motionToUvY * float(m_inputHeight)};
    prepare.frameTimeDelta = (std::max)(m_inputs.frameTimeDelta, 0.1f);
    prepare.reset = m_inputs.reset || m_resetHistory;
    prepare.cameraNear = m_inputs.cameraNear;
    prepare.cameraFar = m_inputs.cameraFar;
    prepare.cameraFovAngleVertical = m_inputs.verticalFov;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.depth = FfxShared(slot.inputs12[0].Get());
    prepare.motionVectors = FfxShared(slot.inputs12[1].Get());
    memcpy(prepare.cameraPosition, m_inputs.cameraPosition, sizeof(prepare.cameraPosition));
    memcpy(prepare.cameraUp, m_inputs.cameraUp, sizeof(prepare.cameraUp));
    memcpy(prepare.cameraRight, m_inputs.cameraRight, sizeof(prepare.cameraRight));
    memcpy(prepare.cameraForward, m_inputs.cameraForward, sizeof(prepare.cameraForward));
    return m_ffx->Dispatch(&m_fsrFrameGeneration, &prepare.header) == FFX_API_RETURN_OK ? S_OK : E_FAIL;
}

HRESULT D3D12PresentBridge::CreateSharedFence(ComPtr<ID3D12Fence>& fence12, ComPtr<ID3D11Fence>& fence11)
{
    BRIDGE_CHECK(m_device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)));
    SharedHandle handle;
    BRIDGE_CHECK(m_device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    BRIDGE_CHECK(m_device11->OpenSharedFence(handle.value, IID_PPV_ARGS(&fence11)));
    return S_OK;
}

HRESULT D3D12PresentBridge::CreateSharedTexture(UINT width, UINT height, DXGI_FORMAT format, bool unorderedAccess,
    ComPtr<ID3D12Resource>& texture12, ComPtr<ID3D11Texture2D>& texture11)
{
    texture11.Reset();
    texture12.Reset();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = width;
    resource.Height = height;
    resource.DepthOrArraySize = resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Format = format;
    // Keep the render-target capability even for copy/UAV-only textures: native
    // DX11 OpenSharedResource1 rejects these DX12 textures without it on the
    // supported interop path (E_INVALIDARG on RTX 4070).
    resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        (unorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    BRIDGE_CHECK(m_device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &resource,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture12)));
    SharedHandle handle;
    BRIDGE_CHECK(m_device12->CreateSharedHandle(texture12.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    BRIDGE_CHECK(m_device11->OpenSharedResource1(handle.value, IID_PPV_ARGS(&texture11)));
    return S_OK;
}

HRESULT D3D12PresentBridge::CreateSurfaces()
{
    m_slots.resize(m_desc.BufferCount);
    for (UINT i = 0; i < m_desc.BufferCount; ++i)
    {
        auto& slot = m_slots[i];
        BRIDGE_CHECK(CreateSharedTexture(m_desc.Width, m_desc.Height, m_desc.Format, false, slot.shared12, slot.shared11));
        BRIDGE_CHECK(m_device11->CreateRenderTargetView(slot.shared11.Get(), nullptr, &slot.view11));
        BRIDGE_CHECK(m_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator)));
        BRIDGE_CHECK(m_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr, IID_PPV_ARGS(&slot.commands)));
        BRIDGE_CHECK(slot.commands->Close());
    }
    if (m_profiling)
    {
        BRIDGE_CHECK(m_queue->GetTimestampFrequency(&m_timestampFrequency));
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = UINT(m_slots.size()) * 2;
        BRIDGE_CHECK(m_device12->CreateQueryHeap(&query, IID_PPV_ARGS(&m_timestampHeap)));
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource.Width = query.Count * sizeof(UINT64);
        resource.Height = resource.DepthOrArraySize = resource.MipLevels = 1;
        resource.SampleDesc.Count = 1;
        resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        BRIDGE_CHECK(m_device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_timestampReadback)));
    }
    return S_OK;
}

HRESULT D3D12PresentBridge::WaitFor(ID3D12Fence* fence, UINT64 value, bool allocatorReuse)
{
    if (!value) return S_OK;
    const auto completed = fence->GetCompletedValue();
    if (completed == (std::numeric_limits<UINT64>::max)())
        return Check(DXGI_ERROR_DEVICE_REMOVED, "D3D12 fence device removed");
    if (completed >= value) return S_OK;
    LARGE_INTEGER start{}, end{};
    if (allocatorReuse)
    {
        ++m_statistics.allocatorWaits;
        if (m_profiling) QueryPerformanceCounter(&start);
    }
    ResetEvent(m_fenceEvent);
    BRIDGE_CHECK(fence->SetEventOnCompletion(value, m_fenceEvent));
    const DWORD wait = WaitForSingleObject(m_fenceEvent, 5000);
    if (allocatorReuse && m_profiling)
    {
        QueryPerformanceCounter(&end);
        const UINT64 elapsed = end.QuadPart - start.QuadPart;
        m_statistics.allocatorWaitTicks += elapsed;
        m_statistics.maxAllocatorWaitTicks = (std::max)(m_statistics.maxAllocatorWaitTicks, elapsed);
    }
    if (wait == WAIT_OBJECT_0)
        return fence->GetCompletedValue() == (std::numeric_limits<UINT64>::max)() ?
            Check(DXGI_ERROR_DEVICE_REMOVED, "D3D12 fence device removed after wait") : S_OK;
    return Check(wait == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(WAIT_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError()), "D3D12 fence wait");
}

HRESULT D3D12PresentBridge::AcquireFrame()
{
    if (m_acquired) return m_submitted ? Check(E_UNEXPECTED, "Present required before next acquire") : S_OK;
    if (m_slots.empty()) return Check(E_UNEXPECTED, "Bridge not initialized");
    m_currentSlot = m_nextSlot;
    auto& slot = m_slots[m_currentSlot];
    // DX11 is about to rewrite this slot's FG inputs. DLSS-G finished reading
    // them frames ago in practice, so this rarely waits; when it does, it waits
    // on the CPU, which cannot block DLSS-G's own queue (see AfterPresent).
    if (slot.inputFence)
    {
        BRIDGE_CHECK(WaitFor(slot.inputFence.Get(), slot.inputValue));
        slot.inputFence.Reset();
        slot.inputValue = 0;
    }
    auto* fence12 = slot.retired ? m_retired12.Get() : m_finished12.Get();
    auto* fence11 = slot.retired ? m_retired11.Get() : m_finished11.Get();
    const UINT64 value = slot.retired ? slot.retired : slot.finished;
    const UINT64 completed = fence12->GetCompletedValue();
    if (completed == (std::numeric_limits<UINT64>::max)())
        return Check(DXGI_ERROR_DEVICE_REMOVED, "Acquire fence device removed");
    if (completed < value)
    {
        ++m_statistics.reuseWaits;
        // GPU-only wait before any writes/command lists for this slot, not a CPU stall.
        BRIDGE_CHECK(m_context11->Wait(fence11, value));
    }
    m_acquired = true;
    return S_OK;
}

HRESULT D3D12PresentBridge::TagStreamline(Slot& slot)
{
    sl::Constants constants{};
    static const float identity[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static_assert(sizeof(sl::float4x4) == sizeof(identity), "Streamline matrix layout");
    memcpy(&constants.cameraViewToClip, m_inputs.viewToClip, sizeof(identity));
    memcpy(&constants.clipToCameraView, m_inputs.clipToView, sizeof(identity));
    memcpy(&constants.clipToPrevClip, m_inputs.clipToPrevClip, sizeof(identity));
    memcpy(&constants.prevClipToClip, m_inputs.prevClipToClip, sizeof(identity));
    memcpy(&constants.clipToLensClip, identity, sizeof(identity));
    constants.jitterOffset = {m_inputs.jitterX, m_inputs.jitterY};
    constants.mvecScale = {m_inputs.motionToUvX, m_inputs.motionToUvY};
    constants.cameraPinholeOffset = {0.f, 0.f};
    constants.cameraPos = {m_inputs.cameraPosition[0], m_inputs.cameraPosition[1], m_inputs.cameraPosition[2]};
    constants.cameraUp = {m_inputs.cameraUp[0], m_inputs.cameraUp[1], m_inputs.cameraUp[2]};
    constants.cameraRight = {m_inputs.cameraRight[0], m_inputs.cameraRight[1], m_inputs.cameraRight[2]};
    constants.cameraFwd = {m_inputs.cameraForward[0], m_inputs.cameraForward[1], m_inputs.cameraForward[2]};
    constants.cameraNear = m_inputs.cameraNear;
    constants.cameraFar = m_inputs.cameraFar;
    constants.cameraFOV = m_inputs.verticalFov;
    constants.cameraAspectRatio = m_inputs.aspectRatio;
    constants.depthInverted = sl::eFalse;
    constants.cameraMotionIncluded = sl::eTrue;
    constants.motionVectors3D = constants.motionVectorsJittered = sl::eFalse;
    constants.reset = (m_inputs.reset || m_resetHistory) ? sl::eTrue : sl::eFalse;
    return m_streamline.Tag(constants, slot.inputs12[0].Get(), slot.inputs12[1].Get(), slot.inputs12[2].Get()) ? S_OK : E_FAIL;
}

HRESULT D3D12PresentBridge::SubmitFrame()
{
    if (!m_acquired || m_submitted) return Check(E_UNEXPECTED, "Acquire/render/submit/present order");
    bool generate = NeedsFGInputs() && m_sceneValid && m_hudlessValid;
    const bool streamline = m_runtimeKind == PresentRuntime::Streamline && m_streamline.IsReady();
    if (streamline && !m_streamline.SetEnabled(generate))
    {
        // Turn FG off and keep presenting; fatal only if DLSS-G cannot be disabled.
        BRIDGE_CHECK(FailGeneration("DLSS-G on/off switch"));
        generate = false;
    }
    const UINT index = m_swapChain->GetCurrentBackBufferIndex();
    if (index >= m_slots.size()) return Check(E_UNEXPECTED, "Invalid D3D12 backbuffer index");
    auto& slot = m_slots[m_currentSlot];
    // Only allocator reuse may wait on the CPU. Pixel transport never involves CPU readback.
    BRIDGE_CHECK(WaitFor(m_finished12.Get(), slot.finished, true));
    if (slot.timestampPending)
    {
        const SIZE_T offset = m_currentSlot * 2 * sizeof(UINT64);
        D3D12_RANGE range{offset, offset + 2 * sizeof(UINT64)};
        void* data{};
        BRIDGE_CHECK(m_timestampReadback->Map(0, &range, &data));
        const auto* ticks = reinterpret_cast<const UINT64*>(static_cast<const char*>(data) + offset);
        m_statistics.copyTicks += ticks[1] - ticks[0];
        ++m_statistics.copySamples;
        D3D12_RANGE written{0, 0};
        m_timestampReadback->Unmap(0, &written);
        slot.timestampPending = false;
    }
    // Proxy swapchains may replace their virtual back buffers when interpolation changes state.
    // Always acquire the current proxy buffer, never retain an old native buffer across a toggle.
    slot.backBuffer.Reset();
    BRIDGE_CHECK(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&slot.backBuffer)));
    BRIDGE_CHECK(slot.allocator->Reset());
    BRIDGE_CHECK(slot.commands->Reset(slot.allocator.Get(), nullptr));

    const UINT64 serial = ++m_serial;
    BRIDGE_CHECK(m_context11->Signal(m_ready11.Get(), serial));
    m_lastReady = serial;
    m_context11->Flush(); // Submit the signal before D3D12 waits for it.
    BRIDGE_CHECK(m_queue->Wait(m_ready12.Get(), serial));

    if (m_profiling) slot.commands->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_currentSlot * 2);
    D3D12_RESOURCE_BARRIER barriers[]{
        Transition(slot.shared12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
        Transition(slot.backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)};
    slot.commands->ResourceBarrier(2, barriers);
    slot.commands->CopyResource(slot.backBuffer.Get(), slot.shared12.Get());
    barriers[0] = Transition(slot.shared12.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    barriers[1] = Transition(slot.backBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    slot.commands->ResourceBarrier(2, barriers);
    if (m_profiling)
    {
        slot.commands->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_currentSlot * 2 + 1);
        slot.commands->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_currentSlot * 2, 2,
            m_timestampReadback.Get(), m_currentSlot * 2 * sizeof(UINT64));
        slot.timestampPending = true;
    }
    // FSR prepare is recorded into the same list, after the DX11 ready wait.
    bool fsrFailed = false;
    if (m_runtimeKind == PresentRuntime::Fsr && m_fsrFrameGeneration && !m_fgFailed)
    {
        fsrFailed = FAILED(ConfigureFsr(generate, slot));
        if (fsrFailed) generate = false;
    }
    BRIDGE_CHECK(slot.commands->Close());
    ID3D12CommandList* lists[]{slot.commands.Get()};
    m_queue->ExecuteCommandLists(1, lists);
    BRIDGE_CHECK(m_queue->Signal(m_finished12.Get(), serial));
    slot.finished = serial;
    m_submitted = true;
    m_submittedFG = generate;
    ++m_statistics.frames;
    m_statistics.colorBytes += UINT64(m_desc.Width) * m_desc.Height * 4;
    if (fsrFailed) BRIDGE_CHECK(FailGeneration("FSR frame-generation configure/prepare"));
    if (streamline)
    {
        if (generate)
        {
            if (FAILED(TagStreamline(slot))) BRIDGE_CHECK(FailGeneration("FG constants/tags/options"));
        }
        else if (!m_streamline.ClearTags()) BRIDGE_CHECK(FailGeneration("Suspend FG"));
    }
    m_resetHistory = !generate;
    return S_OK;
}

HRESULT D3D12PresentBridge::BeginFrame(UINT frameLimitUs, bool allowGeneration)
{
    m_allowGeneration = allowGeneration;
    m_sceneValid = m_hudlessValid = false;
    if (m_runtimeKind != PresentRuntime::Streamline || !m_streamline.IsReady() || m_fgFailed) return S_OK;
    return m_streamline.BeginFrame(frameLimitUs) ? S_OK : FailGeneration("Begin Reflex frame");
}

bool D3D12PresentBridge::NeedsFGInputs() const
{
    if (m_fgFailed || !m_allowGeneration) return false;
    if (m_runtimeKind == PresentRuntime::Streamline)
        return m_streamline.IsReady() && m_streamline.HasFrame() && m_streamline.CanGenerate(m_desc.Width, m_desc.Height);
    return m_runtimeKind == PresentRuntime::Fsr && m_fsrFrameGeneration != nullptr;
}

HRESULT D3D12PresentBridge::CreateInputs(UINT width, UINT height, DXGI_FORMAT motionFormat)
{
    BRIDGE_CHECK(WaitIdle());
    // DLSS-G may still read the old inputs after our queue drains; release them
    // only once its input fences have completed.
    for (auto& slot : m_slots)
    {
        if (slot.inputFence) BRIDGE_CHECK(WaitFor(slot.inputFence.Get(), slot.inputValue));
        slot.inputFence.Reset();
        slot.inputValue = 0;
    }
    if (m_runtimeKind == PresentRuntime::Streamline && !m_streamline.ClearTags()) return Check(E_FAIL, "Clear resized FG inputs");
    m_depthSRV.Reset();
    m_depthSource.Reset();
    const DXGI_FORMAT formats[]{DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT, m_desc.Format};
    for (auto& slot : m_slots)
    {
        slot.depthUAV.Reset();
        slot.motionUAV.Reset();
        for (UINT i = 0; i < 3; ++i)
            BRIDGE_CHECK(CreateSharedTexture(i == 2 ? m_desc.Width : width, i == 2 ? m_desc.Height : height,
                formats[i], i < 2, slot.inputs12[i], slot.inputs11[i]));
        BRIDGE_CHECK(m_device11->CreateUnorderedAccessView(slot.inputs11[0].Get(), nullptr, &slot.depthUAV));
        BRIDGE_CHECK(m_device11->CreateUnorderedAccessView(slot.inputs11[1].Get(), nullptr, &slot.motionUAV));
    }
    m_inputWidth = width;
    m_inputHeight = height;
    m_motionFormat = motionFormat;
    m_resetHistory = true;
    ++m_statistics.inputRebuilds;
    return S_OK;
}

HRESULT D3D12PresentBridge::ConvertDepthMotion(ID3D11Texture2D* depth, ID3D11Texture2D* motion,
    ID3D11UnorderedAccessView* depthOut, ID3D11UnorderedAccessView* motionOut, UINT width, UINT height, bool sample,
    ID3D11Resource* copyTarget, ID3D11Resource* copySource)
{
    if (!m_captureState)
    {
        const D3D_FEATURE_LEVEL level = m_device11->GetFeatureLevel();
        D3D_FEATURE_LEVEL selected{};
        BRIDGE_CHECK(m_device11->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
            __uuidof(ID3D11Device), &selected, &m_captureState));
    }
    // Use a private context state, including OM/DSV bindings. Merely saving CS
    // bindings leaves the source depth attached as a DSV and silently nulls its SRV.
    struct RestoreState
    {
        ID3D11DeviceContext1* context;
        ComPtr<ID3DDeviceContextState> previous;
        ~RestoreState() { context->SwapDeviceContextState(previous.Get(), nullptr); }
    } restore{m_context11.Get()};
    m_context11->SwapDeviceContextState(m_captureState.Get(), &restore.previous);
    m_context11->ClearState();
    if (copyTarget && copySource) m_context11->CopyResource(copyTarget, copySource);
    if (!m_depthShader)
    {
        static constexpr char source[] = R"(
Texture2D<float> sourceDepth : register(t0);
Texture2D<float4> sourceMotion : register(t1);
RWTexture2D<float> outputDepth : register(u0);
RWTexture2D<float2> outputMotion : register(u1);
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID)
{
    uint w, h; outputDepth.GetDimensions(w,h);
    if (p.x < w && p.y < h) {
        outputDepth[p.xy] = sourceDepth.Load(int3(p.xy,0));
        outputMotion[p.xy] = sourceMotion.Load(int3(p.xy,0)).xy;
    }
})";
        ComPtr<ID3DBlob> shader, errors;
        BRIDGE_CHECK(D3DCompile(source, sizeof(source)-1, "FG input conversion", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors));
        BRIDGE_CHECK(m_device11->CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &m_depthShader));
    }
    if (m_depthSource.Get() != depth)
    {
        m_depthSRV.Reset(); m_depthSource.Reset(); m_depthCopy.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        depth->GetDesc(&desc);
        // Native rendering's depth need not be SRV-readable. Copy into a typeless
        // resource in that case; upscaling's existing readable depth needs no copy.
        if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
        {
            desc.Format = (desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT) ? DXGI_FORMAT_R24G8_TYPELESS : desc.Format;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = desc.CPUAccessFlags = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            BRIDGE_CHECK(m_device11->CreateTexture2D(&desc, nullptr, &m_depthCopy));
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Format = desc.Format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R32_FLOAT;
        BRIDGE_CHECK(m_device11->CreateShaderResourceView(m_depthCopy ? m_depthCopy.Get() : depth, &view, &m_depthSRV));
        m_depthSource = depth;
    }
    if (m_depthCopy) m_context11->CopyResource(m_depthCopy.Get(), depth);
    if (m_motionSource.Get() != motion)
    {
        m_motionSRV.Reset(); m_motionSource.Reset();
        BRIDGE_CHECK(m_device11->CreateShaderResourceView(motion, nullptr, &m_motionSRV));
        m_motionSource = motion;
    }
    ID3D11ShaderResourceView* srvs[]{m_depthSRV.Get(), m_motionSRV.Get()};
    ID3D11UnorderedAccessView* uavs[]{depthOut, motionOut};
    m_context11->CSSetShader(m_depthShader.Get(), nullptr, 0);
    m_context11->CSSetShaderResources(0, 2, srvs);
    m_context11->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    m_context11->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    ID3D11UnorderedAccessView* nullUAVs[2]{};
    m_context11->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    if (sample && m_profiling) BRIDGE_CHECK(SampleInputs());
    m_context11->ClearState();
    return S_OK;
}

HRESULT D3D12PresentBridge::SampleInputs()
{
    if (m_diagnosticPending)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = m_context11->Map(m_diagnosticReadback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (SUCCEEDED(result))
        {
            float values[12]{};
            memcpy(values, mapped.pData, sizeof(values));
            m_context11->Unmap(m_diagnosticReadback.Get(), 0);
            m_diagnosticPending = false;
            if (m_logger)
            {
                char message[512]{};
                snprintf(message, sizeof(message), "[FG DIAG] Input sample frame=%llu depth=[%.6f,%.6f] motionX=[%.6f,%.6f] motionY=[%.6f,%.6f] nonfinite=%.0f moving=%.0f/256 depthOutside01=%.0f. Sparse source sample; not a visual-quality verdict.",
                    m_diagnosticFrame, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8]);
                m_logger(message);
            }
        }
        else if (result != DXGI_ERROR_WAS_STILL_DRAWING) return Check(result, "FG diagnostic readback");
    }
    if (m_diagnosticPending || m_statistics.sceneCaptures % 240 != 0) return S_OK;
    if (!m_diagnosticShader)
    {
        static constexpr char source[] = R"(
Texture2D<float> depthInput : register(t0);
Texture2D<float4> motionInput : register(t1);
RWStructuredBuffer<float4> result : register(u0);
[numthreads(1,1,1)] void main()
{
    uint w,h; depthInput.GetDimensions(w,h);
    float dmin=1, dmax=0; float2 vmin=1e30, vmax=-1e30;
    float bad=0, moving=0, outside=0;
    for (uint y=0;y<16;y++) for (uint x=0;x<16;x++) {
        uint2 p=min(uint2((x+0.5)*w/16, (y+0.5)*h/16),uint2(w-1,h-1));
        float d=depthInput.Load(int3(p,0)); float2 v=motionInput.Load(int3(p,0)).xy;
        if (!isfinite(d) || !all(isfinite(v))) { bad++; continue; }
        dmin=min(dmin,d); dmax=max(dmax,d); vmin=min(vmin,v); vmax=max(vmax,v);
        moving+=any(abs(v)>1e-6); outside+=(d<0 || d>1);
    }
    result[0]=float4(dmin,dmax,vmin.x,vmax.x);
    result[1]=float4(vmin.y,vmax.y,bad,moving);
    result[2]=float4(outside,0,0,0);
})";
        ComPtr<ID3DBlob> shader, errors;
        BRIDGE_CHECK(D3DCompile(source, sizeof(source)-1, "FG sparse diagnostics", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors));
        BRIDGE_CHECK(m_device11->CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &m_diagnosticShader));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=48; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride=16;
        BRIDGE_CHECK(m_device11->CreateBuffer(&desc, nullptr, &m_diagnosticBuffer));
        BRIDGE_CHECK(m_device11->CreateUnorderedAccessView(m_diagnosticBuffer.Get(), nullptr, &m_diagnosticUAV));
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        BRIDGE_CHECK(m_device11->CreateBuffer(&desc, nullptr, &m_diagnosticReadback));
    }
    m_context11->CSSetShader(m_diagnosticShader.Get(), nullptr, 0);
    auto* uav=m_diagnosticUAV.Get();
    m_context11->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    m_context11->Dispatch(1,1,1);
    uav=nullptr; m_context11->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    m_context11->CopyResource(m_diagnosticReadback.Get(),m_diagnosticBuffer.Get());
    m_diagnosticPending=true; m_diagnosticFrame=m_statistics.frames;
    return S_OK;
}


HRESULT D3D12PresentBridge::CaptureScene(ID3D11Resource* depth, ID3D11Resource* motion, const FrameGenerationInputs& inputs)
{
    if (!NeedsFGInputs()) return S_OK;
    if (!m_acquired || m_submitted) return Check(E_UNEXPECTED, "FG capture outside acquired frame");
    if (!depth || !motion) return Check(E_INVALIDARG, "Null FG scene input");
    ComPtr<ID3D11Texture2D> depthTexture, motionTexture;
    BRIDGE_CHECK(depth->QueryInterface(IID_PPV_ARGS(&depthTexture)));
    BRIDGE_CHECK(motion->QueryInterface(IID_PPV_ARGS(&motionTexture)));
    D3D11_TEXTURE2D_DESC d{}, m{};
    depthTexture->GetDesc(&d);
    motionTexture->GetDesc(&m);
    if (d.Width != m.Width || d.Height != m.Height || d.SampleDesc.Count != 1 || m.SampleDesc.Count != 1 ||
        d.ArraySize != 1 || m.ArraySize != 1 || d.MipLevels != 1 || m.MipLevels != 1 ||
        (d.Format != DXGI_FORMAT_R24G8_TYPELESS && d.Format != DXGI_FORMAT_R32_TYPELESS && d.Format != DXGI_FORMAT_R32_FLOAT && d.Format != DXGI_FORMAT_D24_UNORM_S8_UINT) ||
        m.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return Check(E_INVALIDARG, "Unsupported FG input dimensions/format");
    auto& slot = m_slots[m_currentSlot];
    if (!slot.inputs11[0] || d.Width != m_inputWidth || d.Height != m_inputHeight || m.Format != m_motionFormat)
        BRIDGE_CHECK(CreateInputs(d.Width, d.Height, m.Format));
    BRIDGE_CHECK(ConvertDepthMotion(depthTexture.Get(), motionTexture.Get(), slot.depthUAV.Get(), slot.motionUAV.Get(),
        m_inputWidth, m_inputHeight, true));
    m_inputs = inputs;
    m_sceneValid = true;
    ++m_statistics.sceneCaptures;
    m_statistics.inputCopyBytes += UINT64(d.Width) * d.Height * 8;
    return S_OK;
}

HRESULT D3D12PresentBridge::CaptureHudless(bool valid)
{
    m_hudlessValid = NeedsFGInputs() && valid && m_sceneValid;
    if (m_hudlessValid)
    {
        if (!m_acquired || m_submitted) return Check(E_UNEXPECTED, "HUD-less capture outside acquired frame");
        auto& slot = m_slots[m_currentSlot];
        m_context11->CopyResource(slot.inputs11[2].Get(), slot.shared11.Get());
        ++m_statistics.hudlessCaptures;
        m_statistics.inputCopyBytes += UINT64(m_desc.Width) * m_desc.Height * 4;
    }
    return S_OK;
}

HRESULT D3D12PresentBridge::SetGeneration(bool enabled)
{
    if (m_runtimeKind == PresentRuntime::Streamline && m_streamline.IsReady())
        return m_streamline.SetEnabled(enabled) && (enabled || m_streamline.ClearTags()) ? S_OK : E_FAIL;
    if (m_runtimeKind == PresentRuntime::Fsr && m_fsrFrameGeneration && !enabled)
    {
        ffxConfigureDescFrameGeneration off{};
        off.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        off.swapChain = m_swapChain.Get();
        off.frameGenerationEnabled = false;
        off.frameID = m_fsrFrameId;
        return m_ffx->Configure(&m_fsrFrameGeneration, &off.header) == FFX_API_RETURN_OK ? S_OK : E_FAIL;
    }
    return S_OK;
}

HRESULT D3D12PresentBridge::SuspendGeneration()
{
    if (m_runtimeKind == PresentRuntime::Plain) return WaitIdle();
    if (m_logger) m_logger("[FG DIAG] SuspendGeneration begin: requesting Off, then draining GPU work");
    // A runtime that will not suspend is disabled instead (fatal only if that fails too).
    if (FAILED(SetGeneration(false))) BRIDGE_CHECK(FailGeneration("Suspend FG for window transition"));
    m_sceneValid = m_hudlessValid = false;
    m_resetHistory = true;
    const HRESULT result = WaitIdle();
    if (m_logger)
    {
        char diagnostic[128]{};
        snprintf(diagnostic, sizeof(diagnostic), "[FG DIAG] SuspendGeneration end: drain=0x%08X", unsigned(result));
        m_logger(diagnostic);
    }
    return result;
}

HRESULT D3D12PresentBridge::WaitIdle()
{
    if (!m_queue || !m_finished12 || !m_fenceEvent) return S_OK;
    const UINT64 serial = ++m_serial;
    // Include pending native DX11 work (e.g. captures before a resize), not only
    // frames which have already reached SubmitFrame.
    if (m_context11 && m_ready11 && m_ready12)
    {
        BRIDGE_CHECK(m_context11->Signal(m_ready11.Get(), serial));
        m_context11->Flush();
        BRIDGE_CHECK(m_queue->Wait(m_ready12.Get(), serial));
    }
    BRIDGE_CHECK(m_queue->Signal(m_finished12.Get(), serial));
    // This also drains the FG input waits inserted by AfterPresent.
    return WaitFor(m_finished12.Get(), serial);
}

void D3D12PresentBridge::SampleDisplayedFrames()
{
    if (!m_profiling || !m_logger || m_statistics.frames % 240 != 0) return;
    // Diagnostic only: whether a proxy reports native presents is runtime-specific.
    // Frame statistics are unavailable for composed windowed presents, so fall back
    // to the swapchain's present counter.
    DXGI_FRAME_STATISTICS stats{};
    UINT presents = 0;
    const char* source = "frame statistics";
    if (SUCCEEDED(m_swapChain->GetFrameStatistics(&stats))) presents = stats.PresentCount;
    else if (SUCCEEDED(m_swapChain->GetLastPresentCount(&presents))) source = "present counter";
    else return;
    if (m_displayFrameBase && presents >= m_displayPresentBase && m_statistics.frames > m_displayFrameBase)
    {
        char message[256]{};
        snprintf(message, sizeof(message), "[FG DIAG] DXGI %s +%u over %llu rendered presents (%.2f per rendered frame; ~2 when generating, if the runtime reports native presents).",
            source, presents - UINT(m_displayPresentBase), m_statistics.frames - m_displayFrameBase,
            double(presents - m_displayPresentBase) / double(m_statistics.frames - m_displayFrameBase));
        m_logger(message);
    }
    m_displayPresentBase = presents;
    m_displayFrameBase = m_statistics.frames;
}

HRESULT D3D12PresentBridge::AfterPresent()
{
    if (!m_submitted) return Check(E_UNEXPECTED, "AfterPresent without submission");
    auto& slot = m_slots[m_currentSlot];
    if (m_runtimeKind == PresentRuntime::Streamline && m_streamline.IsReady())
    {
        ComPtr<ID3D12Fence> inputFence;
        UINT64 inputValue{};
        const bool healthy = m_streamline.AfterPresent(&inputFence, inputValue);
        slot.retired = 0;
        if (!healthy)
        {
            // In D3D12's eBlockPresentingClientQueue mode, the presenting queue
            // is ordered behind FG work even when GetState supplies no fence.
            // A one-time CPU drain therefore protects DX11 reuse of every slot.
            // Keep valid explicit fence data too, when the failed query supplied it.
            BRIDGE_CHECK(m_device12->GetDeviceRemovedReason());
            BRIDGE_CHECK(m_device11->GetDeviceRemovedReason());
            // CPU waits only: a GPU wait on DLSS-G's fence could block the queue it needs.
            if (inputFence && inputValue) BRIDGE_CHECK(WaitFor(inputFence.Get(), inputValue));
            for (auto& pending : m_slots)
                if (pending.inputFence) BRIDGE_CHECK(WaitFor(pending.inputFence.Get(), pending.inputValue));
            BRIDGE_CHECK(WaitIdle());
            BRIDGE_CHECK(m_device12->GetDeviceRemovedReason());
            if (!m_streamline.DisableAfterFailure()) return Check(E_FAIL, "Disable failed FG runtime");

            // GPU work is complete and tags are cleared. Free only FG inputs;
            // keep final-color targets, swapchain and Reflex for normal presentation.
            for (auto& retiredSlot : m_slots)
            {
                retiredSlot.inputFence.Reset();
                retiredSlot.inputValue = 0;
                retiredSlot.depthUAV.Reset();
                retiredSlot.motionUAV.Reset();
                for (auto& input : retiredSlot.inputs11) input.Reset();
                for (auto& input : retiredSlot.inputs12) input.Reset();
            }
            m_depthSRV.Reset();
            m_depthSource.Reset();
            m_inputWidth = m_inputHeight = 0;
            m_fgFailed = true;
            m_submittedFG = false;
            m_resetHistory = true;
            if (m_logger) m_logger("! [DLSS FG] Disabled after a runtime failure; continuing without frame generation; it can be turned on again in Video settings.");
        }
        if (m_submittedFG)
        {
            ++m_enabledPresents;
            const UINT presented = m_streamline.PresentedFrames();
            m_totalPresented += presented;
            if (presented > 1) ++m_generatedPresents;
            const UINT64 serial = ++m_serial;
            // DX11 writes on a non-presenting queue, so slot reuse must also follow
            // FG consumption, not only our color copy. The presenting queue is
            // ordered behind FG work (eBlockPresentingClientQueue), so a signal
            // here covers that. DLSS-G's input fence is checked on the CPU at reuse
            // (AcquireFrame) and is never waited on by this queue: if that value
            // depends on a later present (e.g. RTX 50 frame pacing), a queue wait
            // here would block the very present that completes it.
            BRIDGE_CHECK(m_queue->Signal(m_retired12.Get(), serial));
            slot.retired = m_lastRetired = serial;
            if (inputFence && inputValue)
            {
                slot.inputFence = inputFence;
                slot.inputValue = inputValue;
            }
        }
        // Untagged Off/probe frames need only the color-copy fence; no redundant
        // retirement signals or waits for a previous frame's FG inputs.
    }
    else if (m_runtimeKind == PresentRuntime::Fsr)
    {
        slot.retired = 0;
        if (m_submittedFG)
        {
            // Synchronous FSR generation ran on this queue during Present.
            ++m_enabledPresents;
            const UINT64 serial = ++m_serial;
            BRIDGE_CHECK(m_queue->Signal(m_retired12.Get(), serial));
            slot.retired = m_lastRetired = serial;
        }
        // Frame IDs must advance by exactly one per presented frame.
        ++m_fsrFrameId;
    }
    m_lastGenerated = m_submittedFG;
    // Keep CurrentSlot pointing to the just-presented frame for screenshots until
    // the renderer explicitly acquires its next frame.
    m_nextSlot = (m_currentSlot + 1) % UINT(m_slots.size());
    m_acquired = m_submitted = m_submittedFG = false;
    m_sceneValid = m_hudlessValid = false;
    SampleDisplayedFrames();
    if (m_profiling && m_statistics.frames % 240 == 0) ReportStatistics();
    return S_OK;
}

// The player turned frame generation back on after an automatic disable. FG work
// was drained and the runtime switched off by FailGeneration/AfterPresent; start
// again from clean state (DLSS-G re-allocates, FG inputs are rebuilt on capture).
void D3D12PresentBridge::ClearGenerationFailure()
{
    if (!m_fgFailed) return;
    m_fgFailed = false;
    m_resetHistory = true;
    if (m_runtimeKind == PresentRuntime::Streamline) m_streamline.ClearFailure();
}

HRESULT D3D12PresentBridge::FailGeneration(const char* reason)
{
    if (m_fgFailed) return S_OK;
    // The presenting queue is ordered behind FG. Drain it before clearing tags
    // or permitting DX11 to reuse an input whose completion fence was missing.
    BRIDGE_CHECK(WaitIdle());
    if (m_runtimeKind == PresentRuntime::Streamline && m_streamline.IsReady() && !m_streamline.DisableAfterFailure())
        return Check(E_FAIL, "Cannot disable failed FG runtime safely");
    if (m_runtimeKind == PresentRuntime::Fsr) SetGeneration(false);
    m_fgFailed = true;
    m_submittedFG = m_sceneValid = m_hudlessValid = false;
    m_resetHistory = true;
    if (m_logger)
    {
        char message[512]{};
        snprintf(message,sizeof(message),"! [%s FG] Disabled: %s. Continuing without frame generation; it can be turned on again in Video settings.",
            m_runtimeKind == PresentRuntime::Fsr ? "FSR" : "DLSS", reason);
        m_logger(message);
    }
    return S_OK;
}

HRESULT D3D12PresentBridge::Present(UINT interval, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST) return m_swapChain->Present(interval, flags);
    // FSR's swapchain paces generated frames with VSync on; Streamline reports support.
    m_vsyncBlocked = m_runtimeKind == PresentRuntime::Streamline && m_streamline.IsReady() && interval && !m_streamline.SupportsVSync();
    if (m_vsyncBlocked) m_allowGeneration = false;
    if (auto* sl = GetStreamline()) sl->Marker(sl::PCLMarker::eRenderSubmitEnd);
    BRIDGE_CHECK(SubmitFrame());
    if (auto* sl = GetStreamline()) sl->Marker(sl::PCLMarker::ePresentStart);
    const HRESULT result = m_swapChain->Present(interval, flags);
    if (auto* sl = GetStreamline()) sl->Marker(sl::PCLMarker::ePresentEnd);
    BRIDGE_CHECK(result);
    BRIDGE_CHECK(AfterPresent());
    return result;
}

HRESULT D3D12PresentBridge::Resize(UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    if (!width || !height) return Check(E_INVALIDARG, "Zero-sized bridge resize");
    BRIDGE_CHECK(SuspendGeneration());
    // FSR's context is display-size specific and must not outlive the old buffers.
    DestroyFsrFrameGeneration();
    BRIDGE_CHECK(WaitIdle());
    m_slots.clear();
    m_depthSRV.Reset();
    m_depthSource.Reset();
    m_inputWidth = m_inputHeight = 0;
    m_timestampHeap.Reset();
    m_timestampReadback.Reset();
    m_sceneValid = m_hudlessValid = false;
    m_resetHistory = true;
    m_acquired = m_submitted = m_submittedFG = false;
    m_currentSlot = m_nextSlot = 0;
    BRIDGE_CHECK(m_swapChain->ResizeBuffers(m_desc.BufferCount, width, height, format, flags));
    m_desc.Format = format;
    m_desc.Flags = flags;
    m_desc.Width = width;
    m_desc.Height = height;
    BRIDGE_CHECK(CreateSurfaces());
    if (m_runtimeKind == PresentRuntime::Fsr && !m_fgFailed && FAILED(CreateFsrFrameGeneration()))
    {
        m_fgFailed = true;
        if (m_logger) m_logger("! [FSR FG] Could not recreate frame generation after a resize; continuing without it.");
    }
    return S_OK;
}

HRESULT D3D12PresentBridge::GetRenderSurface(ID3D11Texture2D** surface) const
{
    return GetRenderSurface(m_currentSlot, surface);
}

HRESULT D3D12PresentBridge::GetRenderSurface(UINT slot, ID3D11Texture2D** surface) const
{
    if (!surface) return E_POINTER;
    *surface = nullptr;
    return slot < m_slots.size() ? m_slots[slot].shared11.CopyTo(surface) : E_INVALIDARG;
}

ID3D12Resource* D3D12PresentBridge::GetCapturedInput(UINT input) const
{
    return input < 3 && m_currentSlot < m_slots.size() ? m_slots[m_currentSlot].inputs12[input].Get() : nullptr;
}

HRESULT D3D12PresentBridge::GetRenderTargetView(ID3D11RenderTargetView** view) const
{
    if (!view) return E_POINTER;
    *view = nullptr;
    return m_currentSlot < m_slots.size() ? m_slots[m_currentSlot].view11.CopyTo(view) : E_UNEXPECTED;
}

HRESULT D3D12PresentBridge::CreateUpscaler(UINT renderWidth, UINT renderHeight, UINT displayWidth, UINT displayHeight, bool hdr)
{
    DestroyUpscaler();
    if (!m_device12 || !m_device11) return Check(E_UNEXPECTED, "FSR upscaler without a DX12 device");
    if (!renderWidth || !renderHeight || renderWidth > displayWidth || renderHeight > displayHeight)
        return Check(E_INVALIDARG, "FSR upscaler dimensions");
    if (!m_ffx->Loaded() && !m_ffx->Load(m_logger, m_profiling)) return Check(E_FAIL, m_ffx->FailureStage());
    auto create = [&]() -> HRESULT
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        BRIDGE_CHECK(m_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_upQueue)));
        m_upQueue->SetName(L"X-Ray AMD FSR upscaling");
        for (auto& allocator : m_upAllocators)
            BRIDGE_CHECK(m_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        BRIDGE_CHECK(m_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_upAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_upCommands)));
        BRIDGE_CHECK(m_upCommands->Close());
        BRIDGE_CHECK(CreateSharedFence(m_upReady12, m_upReady11));
        BRIDGE_CHECK(CreateSharedFence(m_upDone12, m_upDone11));

        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = m_device12.Get();
        ffxCreateContextDescUpscaleVersion version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        version.header.pNext = &backend.header;
        version.version = FFX_UPSCALER_VERSION;
        ffxCreateContextDescUpscale desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        desc.header.pNext = &version.header;
        desc.maxRenderSize = {renderWidth, renderHeight};
        desc.maxUpscaleSize = {displayWidth, displayHeight};
        desc.fpMessage = &FfxApi::Message;
        // LDR at this integration point unless HDR10 is on; FSR 4 is told that LDR
        // input is perceptual. AMD recommends auto exposure when the engine has no
        // exposure texture (the debug checker flags its absence every frame).
        const uint32_t common = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | (hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0) |
            (m_profiling ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0);
        desc.flags = common | (hdr ? 0 : FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE);
        auto result = m_ffx->CreateContext(&m_upContext, &desc.header);
        m_upNonLinear = !hdr && result == FFX_API_RETURN_OK;
        if (result != FFX_API_RETURN_OK && !hdr)
        {
            desc.flags = common;
            result = m_ffx->CreateContext(&m_upContext, &desc.header);
        }
        if (result != FFX_API_RETURN_OK)
        {
            m_upContext = nullptr;
            char message[128]{};
            snprintf(message, sizeof(message), "! [FSR] Upscaler context creation failed (FFX return %u).", unsigned(result));
            if (m_logger) m_logger(message);
            return Check(E_FAIL, "Create AMD FSR upscaling context");
        }
        m_upVersion = m_ffx->ProviderVersion(&m_upContext);
        // The per-dispatch sRGB hint was added with FSR 4; older providers get none.
        m_upNonLinearDispatch = m_upNonLinear && !m_upVersion.empty() && m_upVersion[0] >= '4' && m_upVersion[0] <= '9';

        BRIDGE_CHECK(CreateSharedTexture(renderWidth, renderHeight, DXGI_FORMAT_R32_FLOAT, true, m_upDepth12, m_upDepth11));
        BRIDGE_CHECK(CreateSharedTexture(renderWidth, renderHeight, DXGI_FORMAT_R16G16_FLOAT, true, m_upMotion12, m_upMotion11));
        BRIDGE_CHECK(CreateSharedTexture(displayWidth, displayHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true, m_upOutput12, m_upOutput11));
        BRIDGE_CHECK(m_device11->CreateUnorderedAccessView(m_upDepth11.Get(), nullptr, &m_upDepthUAV));
        BRIDGE_CHECK(m_device11->CreateUnorderedAccessView(m_upMotion11.Get(), nullptr, &m_upMotionUAV));
        return S_OK;
    };
    const HRESULT result = create();
    if (FAILED(result))
    {
        DestroyUpscaler();
        return result;
    }
    m_upHdr = hdr;
    m_upRenderWidth = renderWidth;
    m_upRenderHeight = renderHeight;
    m_upDisplayWidth = displayWidth;
    m_upDisplayHeight = displayHeight;
    if (m_logger)
    {
        char message[256]{};
        snprintf(message, sizeof(message), "[FSR] DX12 upscaler provider %s: scene=%ux%u display=%ux%u hdr=%u perceptual-input=%u (FSR 4 needs Radeon RX 7000/9000; other GPUs use 3.1).",
            m_upVersion.c_str(), renderWidth, renderHeight, displayWidth, displayHeight, unsigned(hdr), unsigned(m_upNonLinear));
        m_logger(message);
    }
    return S_OK;
}

std::string D3D12PresentBridge::FsrDefaultVersion(uint64_t createDescType)
{
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = createDescType;
    query.device = m_device12.Get();
    uint64_t count = 0;
    query.outputCount = &count;
    if (m_ffx->Query(nullptr, &query.header) != FFX_API_RETURN_OK || !count) return {};
    std::vector<uint64_t> ids(count);
    std::vector<const char*> names(count);
    query.versionIds = ids.data();
    query.versionNames = names.data();
    // The first entry is the version a context gets without an override.
    if (m_ffx->Query(nullptr, &query.header) != FFX_API_RETURN_OK || !count || !names[0]) return {};
    return names[0];
}

bool D3D12PresentBridge::FsrRuntimeAvailable()
{
    if (m_fsrAvailable < 0)
    {
        m_fsrAvailable = m_device12 && (m_ffx->Loaded() || m_ffx->Load(m_logger, m_profiling)) ? 1 : 0;
        if (m_fsrAvailable)
        {
            m_fsrUpscalerDefault = FsrDefaultVersion(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE);
            m_fsrFrameGenerationDefault = FsrDefaultVersion(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION);
            m_fsrAvailable = !m_fsrUpscalerDefault.empty() || !m_fsrFrameGenerationDefault.empty();
        }
        if (m_logger)
        {
            char message[192]{};
            snprintf(message, sizeof(message), "[FSR] Runtime %s on this GPU: upscaler %s, frame generation %s.",
                m_fsrAvailable ? "available" : "unavailable",
                m_fsrUpscalerDefault.empty() ? "none" : m_fsrUpscalerDefault.c_str(),
                m_fsrFrameGenerationDefault.empty() ? "none" : m_fsrFrameGenerationDefault.c_str());
            m_logger(message);
        }
    }
    return m_fsrAvailable == 1;
}

const char* D3D12PresentBridge::FsrUpscalerVersion() { return FsrRuntimeAvailable() ? m_fsrUpscalerDefault.c_str() : ""; }
const char* D3D12PresentBridge::FsrFrameGenerationVersion() { return FsrRuntimeAvailable() ? m_fsrFrameGenerationDefault.c_str() : ""; }

void D3D12PresentBridge::DestroyUpscaler()
{
    // The DX11 queue already waited for every submitted upscale; drain the DX12 side
    // before releasing resources the context or its command lists reference.
    if (m_upQueue && m_upDone12 && m_fenceEvent)
    {
        const UINT64 serial = ++m_upSerial;
        if (SUCCEEDED(m_upQueue->Signal(m_upDone12.Get(), serial))) WaitFor(m_upDone12.Get(), serial);
    }
    if (m_upContext)
    {
        m_ffx->DestroyContext(&m_upContext);
        m_upContext = nullptr;
    }
    m_upDepthUAV.Reset();
    m_upMotionUAV.Reset();
    m_upColor11.Reset(); m_upDepth11.Reset(); m_upMotion11.Reset(); m_upOutput11.Reset();
    m_upColor12.Reset(); m_upDepth12.Reset(); m_upMotion12.Reset(); m_upOutput12.Reset();
    m_upCommands.Reset();
    for (auto& allocator : m_upAllocators) allocator.Reset();
    for (auto& value : m_upAllocatorFence) value = 0;
    m_upReady11.Reset(); m_upDone11.Reset(); m_upReady12.Reset(); m_upDone12.Reset();
    m_upQueue.Reset();
    m_upSerial = 0;
    m_upColorFormat = DXGI_FORMAT_UNKNOWN;
    m_upRenderWidth = m_upRenderHeight = m_upDisplayWidth = m_upDisplayHeight = 0;
    m_upVersion.clear();
}

HRESULT D3D12PresentBridge::Upscale(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* motion, const UpscaleInputs& inputs)
{
    if (!m_upContext) return Check(E_UNEXPECTED, "FSR upscale without a context");
    if (!color || !depth || !motion) return Check(E_INVALIDARG, "Null FSR upscale input");
    D3D11_TEXTURE2D_DESC colorDesc{}, depthDesc{}, motionDesc{};
    color->GetDesc(&colorDesc);
    depth->GetDesc(&depthDesc);
    motion->GetDesc(&motionDesc);
    if (colorDesc.Width != m_upRenderWidth || colorDesc.Height != m_upRenderHeight || colorDesc.SampleDesc.Count != 1 ||
        depthDesc.Width != m_upRenderWidth || depthDesc.Height != m_upRenderHeight ||
        motionDesc.Width != m_upRenderWidth || motionDesc.Height != m_upRenderHeight ||
        motionDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return Check(E_INVALIDARG, "FSR upscale input dimensions/format");
    if (!m_upColor11 || m_upColorFormat != colorDesc.Format)
    {
        // DX11 has finished with the old texture: it waited on every prior upscale.
        BRIDGE_CHECK(CreateSharedTexture(m_upRenderWidth, m_upRenderHeight, TypedColorFormat(colorDesc.Format), false, m_upColor12, m_upColor11));
        m_upColorFormat = colorDesc.Format;
    }
    BRIDGE_CHECK(ConvertDepthMotion(depth, motion, m_upDepthUAV.Get(), m_upMotionUAV.Get(), m_upRenderWidth, m_upRenderHeight, false,
        m_upColor11.Get(), color));

    const UINT64 serial = ++m_upSerial;
    BRIDGE_CHECK(m_context11->Signal(m_upReady11.Get(), serial));
    m_context11->Flush();
    BRIDGE_CHECK(m_upQueue->Wait(m_upReady12.Get(), serial));
    const UINT allocatorIndex = UINT(serial % UpscaleAllocators);
    BRIDGE_CHECK(WaitFor(m_upDone12.Get(), m_upAllocatorFence[allocatorIndex], true));
    BRIDGE_CHECK(m_upAllocators[allocatorIndex]->Reset());
    BRIDGE_CHECK(m_upCommands->Reset(m_upAllocators[allocatorIndex].Get(), nullptr));

    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = m_upCommands.Get();
    dispatch.color = FfxShared(m_upColor12.Get());
    dispatch.depth = FfxShared(m_upDepth12.Get());
    dispatch.motionVectors = FfxShared(m_upMotion12.Get());
    dispatch.output = FfxShared(m_upOutput12.Get());
    dispatch.jitterOffset = {inputs.jitterX, inputs.jitterY};
    dispatch.motionVectorScale = {inputs.motionScaleX, inputs.motionScaleY};
    dispatch.renderSize = {m_upRenderWidth, m_upRenderHeight};
    dispatch.upscaleSize = {m_upDisplayWidth, m_upDisplayHeight};
    // The engine's shared CAS runs after upscaling for every method; no RCAS here.
    dispatch.enableSharpening = false;
    dispatch.frameTimeDelta = (std::max)(inputs.frameTimeDelta, 0.1f);
    dispatch.preExposure = 1.0f;
    dispatch.reset = inputs.reset;
    dispatch.cameraNear = inputs.cameraNear;
    dispatch.cameraFar = inputs.cameraFar;
    dispatch.cameraFovAngleVertical = inputs.verticalFov;
    dispatch.viewSpaceToMetersFactor = 1.0f;
    dispatch.flags = m_upNonLinearDispatch ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;
    const auto result = m_ffx->Dispatch(&m_upContext, &dispatch.header);
    BRIDGE_CHECK(m_upCommands->Close());
    if (result == FFX_API_RETURN_OK)
    {
        ID3D12CommandList* lists[]{m_upCommands.Get()};
        m_upQueue->ExecuteCommandLists(1, lists);
    }
    // Always complete the serial so neither queue can wait on a value never signalled.
    BRIDGE_CHECK(m_upQueue->Signal(m_upDone12.Get(), serial));
    m_upAllocatorFence[allocatorIndex] = serial;
    if (result != FFX_API_RETURN_OK)
    {
        char message[128]{};
        snprintf(message, sizeof(message), "! [FSR] Upscale dispatch failed (FFX return %u).", unsigned(result));
        if (m_logger) m_logger(message);
        return Check(E_FAIL, "AMD FSR upscale dispatch");
    }
    // GPU-only wait: DX11 post-processing reads the output after this point.
    BRIDGE_CHECK(m_context11->Wait(m_upDone11.Get(), serial));
    ++m_statistics.upscales;
    return S_OK;
}

void D3D12PresentBridge::ReportStatistics()
{
    if (!m_logger) return;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double usPerTick = 1e6 / double(frequency.QuadPart);
    char message[512]{};
    snprintf(message, sizeof(message), "[DX12 bridge stats] %llu frames, %llu FG captures, %llu DX12 upscales; allocator waits %llu (avg %.1f us, max %.1f us), "
        "GPU slot-reuse waits %llu; DX12 color copy avg %.1f us (%llu samples); color/input copies %.1f/%.1f MiB. Cumulative, not total FG cost.",
        m_statistics.frames, m_statistics.sceneCaptures, m_statistics.upscales, m_statistics.allocatorWaits,
        m_statistics.allocatorWaits ? m_statistics.allocatorWaitTicks * usPerTick / m_statistics.allocatorWaits : 0.,
        m_statistics.maxAllocatorWaitTicks * usPerTick, m_statistics.reuseWaits,
        m_statistics.copySamples ? double(m_statistics.copyTicks) * 1e6 / m_timestampFrequency / m_statistics.copySamples : 0.,
        m_statistics.copySamples, m_statistics.colorBytes / 1048576., m_statistics.inputCopyBytes / 1048576.);
    m_logger(message);
    if (m_runtimeKind == PresentRuntime::Streamline)
        snprintf(message,sizeof(message),"[FG DIAG] Cumulative eligible presents=%llu, presents reporting >1 frame=%llu, actual displayed frames=%llu. Inputs scene/HUDless=%llu/%llu, rebuilds=%llu; failed=%u.",
            m_enabledPresents,m_generatedPresents,m_totalPresented,m_statistics.sceneCaptures,m_statistics.hudlessCaptures,m_statistics.inputRebuilds,unsigned(m_fgFailed));
    else
        snprintf(message,sizeof(message),"[FG DIAG] Cumulative FSR-generating presents=%llu (provider %s). Inputs scene/HUDless=%llu/%llu, rebuilds=%llu; failed=%u.",
            m_enabledPresents,m_fsrFgVersion.empty() ? "none" : m_fsrFgVersion.c_str(),m_statistics.sceneCaptures,m_statistics.hudlessCaptures,m_statistics.inputRebuilds,unsigned(m_fgFailed));
    m_logger(message);
}

void D3D12PresentBridge::Shutdown()
{
    WaitIdle();
    if (m_device12 && m_profiling && m_statistics.frames) ReportStatistics();
    DestroyUpscaler();
    // Runtime shutdown must precede destruction of its DXGI/D3D objects.
    m_streamline.Shutdown();
    DestroyFsrFrameGeneration();
    if (m_fsrSwapChainContext)
    {
        // Releases the context, not the proxy: the final COM release below does that.
        m_ffx->DestroyContext(&m_fsrSwapChainContext);
        m_fsrSwapChainContext = nullptr;
    }
    m_captureState.Reset();
    m_motionSRV.Reset();
    m_motionSource.Reset();
    m_depthCopy.Reset();
    m_diagnosticShader.Reset();
    m_diagnosticBuffer.Reset();
    m_diagnosticReadback.Reset();
    m_diagnosticUAV.Reset();
    m_slots.clear();
    m_depthSRV.Reset();
    m_depthSource.Reset();
    m_timestampHeap.Reset();
    m_timestampReadback.Reset();
    m_depthShader.Reset();
    m_sceneValid = m_hudlessValid = m_allowGeneration = false;
    m_resetHistory = true;
    m_swapChain.Reset();
    m_adapter.Reset();
    m_ready11.Reset();
    m_finished11.Reset();
    m_retired11.Reset();
    m_context11.Reset();
    m_device11.Reset();
    m_ready12.Reset();
    m_finished12.Reset();
    m_retired12.Reset();
    m_queue.Reset();
    m_proxyDevice.Reset();
    m_device12.Reset();
    m_streamline.Unload();
    m_ffx->Unload();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    if (m_runtime) { FreeLibrary(m_runtime); m_runtime = nullptr; }
    m_serial = 0;
    m_currentSlot = m_nextSlot = 0;
    m_acquired = m_submitted = m_submittedFG = m_lastGenerated = false;
    m_inputWidth = m_inputHeight = 0;
    m_motionFormat = DXGI_FORMAT_UNKNOWN;
    m_fsrFrameId = 0;
}

#undef BRIDGE_CHECK
