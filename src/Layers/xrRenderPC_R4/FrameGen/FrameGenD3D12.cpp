#include "FrameGenD3D12.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "../../../../sdk/FidelityFX-API/include/ffx_api/ffx_api.h"
#include "../../../../sdk/FidelityFX-API/include/ffx_api/ffx_framegeneration.h"
#include "../../../../sdk/FidelityFX-API/include/ffx_api/dx12/ffx_api_dx12.h"

namespace framegen
{
using Microsoft::WRL::ComPtr;

namespace
{
// Command allocators in flight on the D3D12 queue
constexpr unsigned kSlots = 3;
// Private D3D12 copies of the inputs: AMD's swapchain may still read the previous frame's set
// while the next one is copied
constexpr unsigned kInputSets = 2;
// CPU wait for the D3D12 queue before an allocator is reused (a hung GPU must not hang forever)
constexpr DWORD kFenceTimeoutMs = 5000;

// Fullscreen triangle copying hardware depth (R24_UNORM_X8 view) into an R32_FLOAT target:
// a D24S8 depth buffer cannot be shared between D3D11 and D3D12
const char kDepthCopyHlsl[] =
    "Texture2D<float> Depth : register(t0);\n"
    "float4 vs_main(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float ps_main(float4 pos : SV_Position) : SV_Target\n"
    "{\n"
    "    return Depth.Load(int3(pos.xy, 0));\n"
    "}\n";

Bridge::Log g_messageLog = nullptr;

// FidelityFX API warnings and errors (ffxConfigureDescGlobalDebug1)
void FfxMessage(uint32_t type, const wchar_t* message)
{
    if (!g_messageLog || !message) return;
    char text[1024];
    char narrow[960];
    WideCharToMultiByte(CP_UTF8, 0, message, -1, narrow, sizeof(narrow), nullptr, nullptr);
    narrow[sizeof(narrow) - 1] = 0;
    std::snprintf(text, sizeof(text), "%s FSR 3.1: %s", type == FFX_API_MESSAGE_TYPE_ERROR ? "!" : "~", narrow);
    g_messageLog(text);
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}
}

// A texture both APIs see: created by D3D12 in a shared heap, opened by D3D11. D3D11 writes it,
// D3D12 only copies from it (COMMON is promoted to COPY_SOURCE and decays back after each
// ExecuteCommandLists, so no barriers are needed on the D3D12 side).
struct Shared
{
    ComPtr<ID3D12Resource> r12;
    ComPtr<ID3D11Texture2D> t11;
    ComPtr<ID3D11RenderTargetView> rtv11;
    unsigned width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool Matches(unsigned w, unsigned h, DXGI_FORMAT f) const { return r12 && width == w && height == h && format == f; }
    void Reset() { rtv11.Reset(); t11.Reset(); r12.Reset(); width = height = 0; format = DXGI_FORMAT_UNKNOWN; }
};

// D3D12-only copy FSR reads, kept in NON_PIXEL_SHADER_RESOURCE (FFX "compute read")
struct Private
{
    ComPtr<ID3D12Resource> r12;
    void Reset() { r12.Reset(); }
};

struct Bridge::Impl
{
    Log log = nullptr;
    std::string error;

    // Libraries
    HMODULE d3d12 = nullptr, ffx = nullptr;
    PfnFfxCreateContext createContext = nullptr;
    PfnFfxDestroyContext destroyContext = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;

    // D3D11 side
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3DDeviceContextState> state11;
    ComPtr<ID3D11VertexShader> depthVs;
    ComPtr<ID3D11PixelShader> depthPs;
    ComPtr<ID3D11Fence> fenceToD3D12_11, fenceToD3D11_11;
    ComPtr<IDXGIFactory2> factory;

    // D3D12 side
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocators[kSlots];
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fenceToD3D12;   // D3D11 signals, the D3D12 queue waits
    ComPtr<ID3D12Fence> fenceToD3D11;   // the D3D12 queue signals, D3D11 and the CPU wait
    HANDLE event = nullptr;
    UINT64 valueToD3D12 = 0, valueToD3D11 = 0;
    UINT64 slotValue[kSlots] = {};
    UINT64 lastSignalToD3D11 = 0;

    // Presentation
    ffxContext swapContext = nullptr, fgContext = nullptr;
    ComPtr<IDXGISwapChain4> swapchain;
    HWND window = nullptr;
    unsigned width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool tearing = false;

    Shared finalFrame, hudless, motion, depth;
    Private privHudless[kInputSets], privMotion[kInputSets], privDepth[kInputSets];

    // Frame state
    Inputs captured;
    bool haveCapture = false;
    bool waitedThisFrame = false;
    UINT64 frameIndex = 0;      // presents through the bridge
    UINT64 frameId = 0;         // FidelityFX frame id, +1 per present
    std::atomic<UINT64> resetFrameId{~0ull};
    bool initialized = false;

    bool Fail(const char* format, ...)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        error = text;
        if (log)
        {
            std::string line = "! [FrameGen] ";
            line += text;
            log(line.c_str());
        }
        return false;
    }

    void Info(const char* format, ...)
    {
        if (!log) return;
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        std::string line = "* [FrameGen] ";
        line += text;
        log(line.c_str());
    }

    // CPU wait until the D3D12 queue reached `value`
    bool WaitCpu(UINT64 value)
    {
        if (!fenceToD3D11 || fenceToD3D11->GetCompletedValue() >= value) return true;
        if (FAILED(fenceToD3D11->SetEventOnCompletion(value, event))) return false;
        if (WaitForSingleObject(event, kFenceTimeoutMs) != WAIT_OBJECT_0)
            return Fail("the D3D12 queue did not finish within %u ms", unsigned(kFenceTimeoutMs));
        return true;
    }

    void WaitIdle()
    {
        if (!queue || !fenceToD3D11) return;
        queue->Signal(fenceToD3D11.Get(), ++valueToD3D11);
        lastSignalToD3D11 = valueToD3D11;
        WaitCpu(valueToD3D11);
    }

    // D3D11 must not overwrite the shared textures before D3D12 copied the previous frame's
    // (a GPU-side wait: the CPU goes on)
    void BeginWrite()
    {
        if (waitedThisFrame) return;
        if (lastSignalToD3D11) context11->Wait(fenceToD3D11_11.Get(), lastSignalToD3D11);
        waitedThisFrame = true;
    }

    bool CreateShared(Shared& s, unsigned w, unsigned h, DXGI_FORMAT f, bool rtv, const wchar_t* name)
    {
        s.Reset();
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = w;
        desc.Height = h;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = f;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        HRESULT hr = device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&s.r12));
        if (FAILED(hr)) return Fail("shared texture %ls %ux%u format %u: CreateCommittedResource 0x%08X", name, w, h, unsigned(f), unsigned(hr));
        s.r12->SetName(name);
        HANDLE handle = nullptr;
        hr = device12->CreateSharedHandle(s.r12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(hr)) return Fail("shared texture %ls: CreateSharedHandle 0x%08X", name, unsigned(hr));
        hr = device11->OpenSharedResource1(handle, IID_PPV_ARGS(&s.t11));
        CloseHandle(handle);
        if (FAILED(hr)) return Fail("shared texture %ls: D3D11 OpenSharedResource1 0x%08X", name, unsigned(hr));
        if (rtv)
        {
            hr = device11->CreateRenderTargetView(s.t11.Get(), nullptr, &s.rtv11);
            if (FAILED(hr)) return Fail("shared texture %ls: render target view 0x%08X", name, unsigned(hr));
        }
        s.width = w;
        s.height = h;
        s.format = f;
        return true;
    }

    bool CreatePrivate(Private& p, unsigned w, unsigned h, DXGI_FORMAT f, const wchar_t* name)
    {
        p.Reset();
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = w;
        desc.Height = h;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = f;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const HRESULT hr = device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&p.r12));
        if (FAILED(hr)) return Fail("texture %ls %ux%u: CreateCommittedResource 0x%08X", name, w, h, unsigned(hr));
        p.r12->SetName(name);
        return true;
    }

    // Shared + private textures of the render-size inputs (they follow the upscaler's size)
    bool EnsureRenderInputs(unsigned w, unsigned h, DXGI_FORMAT motionFormat)
    {
        if (motion.Matches(w, h, motionFormat) && depth.Matches(w, h, DXGI_FORMAT_R32_FLOAT)) return true;
        WaitIdle();
        if (!CreateShared(motion, w, h, motionFormat, false, L"FrameGen shared motion")) return false;
        if (!CreateShared(depth, w, h, DXGI_FORMAT_R32_FLOAT, true, L"FrameGen shared depth")) return false;
        for (unsigned i = 0; i < kInputSets; ++i)
        {
            if (!CreatePrivate(privMotion[i], w, h, motionFormat, L"FrameGen motion")) return false;
            if (!CreatePrivate(privDepth[i], w, h, DXGI_FORMAT_R32_FLOAT, L"FrameGen depth")) return false;
        }
        Info("inputs %ux%u (motion vectors format %u)", w, h, unsigned(motionFormat));
        return true;
    }

    void ReleaseSurfaces()
    {
        finalFrame.Reset(); hudless.Reset(); motion.Reset(); depth.Reset();
        for (unsigned i = 0; i < kInputSets; ++i) { privHudless[i].Reset(); privMotion[i].Reset(); privDepth[i].Reset(); }
    }

    static ffxReturnCode_t DispatchFrameGeneration(ffxDispatchDescFrameGeneration* params, void* user)
    {
        Impl* self = static_cast<Impl*>(user);
        if (params->frameID == self->resetFrameId.load()) params->reset = true;
        return self->dispatch(&self->fgContext, &params->header);
    }
};

Bridge::Bridge() : m(new Impl) {}
Bridge::~Bridge() { Shutdown(); }
bool Bridge::Initialized() const { return m->initialized; }
bool Bridge::Attached() const { return m->swapchain != nullptr && m->fgContext != nullptr; }
const char* Bridge::Error() const { return m->error.c_str(); }

bool Bridge::Initialize(ID3D11Device* device, IDXGIFactory2* factory, Log log)
{
    Shutdown();
    m->log = log;
    g_messageLog = log;
    m->error.clear();
    if (!device || !factory) return m->Fail("no D3D11 device");

    // D3D11 side: shared fences need ID3D11Device5 / ID3D11DeviceContext4 (Windows 10 1703+)
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&m->device11))))
        return m->Fail("needs Windows 10 1703 or newer (no ID3D11Device5)");
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    if (FAILED(immediate.As(&m->context11)))
        return m->Fail("needs Windows 10 1703 or newer (no ID3D11DeviceContext4)");
    m->factory = factory;

    // Libraries, loaded only now: the exe has no link-time dependency on either
    m->d3d12 = LoadLibraryW(L"d3d12.dll");
    if (!m->d3d12) return m->Fail("d3d12.dll is not available (Windows 10 or newer is required)");
    auto createDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(m->d3d12, "D3D12CreateDevice"));
    if (!createDevice) return m->Fail("d3d12.dll has no D3D12CreateDevice");
    m->ffx = LoadLibraryW(L"amd_fidelityfx_dx12.dll");
    if (!m->ffx)
        return m->Fail("amd_fidelityfx_dx12.dll not found next to the exe (install the frame generation add-on)");
    m->createContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(m->ffx, "ffxCreateContext"));
    m->destroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(m->ffx, "ffxDestroyContext"));
    m->configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(m->ffx, "ffxConfigure"));
    m->query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(m->ffx, "ffxQuery"));
    m->dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(m->ffx, "ffxDispatch"));
    if (!m->createContext || !m->destroyContext || !m->configure || !m->query || !m->dispatch)
        return m->Fail("amd_fidelityfx_dx12.dll lacks the FidelityFX API entry points (wrong file?)");

    // D3D12 device on the D3D11 device's adapter
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter11;
    DXGI_ADAPTER_DESC adapterDesc = {};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter11)) ||
        FAILED(adapter11->GetDesc(&adapterDesc)))
        return m->Fail("cannot identify the D3D11 adapter");
    ComPtr<IDXGIFactory4> factory4;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory4))) ||
        FAILED(factory4->EnumAdapterByLuid(adapterDesc.AdapterLuid, IID_PPV_ARGS(&adapter))))
        return m->Fail("cannot find the D3D11 adapter for D3D12");
    HRESULT hr = createDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m->device12));
    if (FAILED(hr)) return m->Fail("D3D12CreateDevice 0x%08X (the GPU or driver has no Direct3D 12)", unsigned(hr));
    m->device12->SetName(L"FrameGen device");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m->device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m->queue));
    if (FAILED(hr)) return m->Fail("CreateCommandQueue 0x%08X", unsigned(hr));
    for (auto& allocator : m->allocators)
    {
        hr = m->device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) return m->Fail("CreateCommandAllocator 0x%08X", unsigned(hr));
    }
    hr = m->device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m->allocators[0].Get(), nullptr, IID_PPV_ARGS(&m->list));
    if (FAILED(hr)) return m->Fail("CreateCommandList 0x%08X", unsigned(hr));
    m->list->Close();

    // Shared fences, one per direction
    for (int i = 0; i < 2; ++i)
    {
        ComPtr<ID3D12Fence>& fence12 = i == 0 ? m->fenceToD3D12 : m->fenceToD3D11;
        ComPtr<ID3D11Fence>& fence11 = i == 0 ? m->fenceToD3D12_11 : m->fenceToD3D11_11;
        hr = m->device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12));
        if (FAILED(hr)) return m->Fail("CreateFence 0x%08X", unsigned(hr));
        HANDLE handle = nullptr;
        hr = m->device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(hr)) return m->Fail("fence CreateSharedHandle 0x%08X", unsigned(hr));
        hr = m->device11->OpenSharedFence(handle, IID_PPV_ARGS(&fence11));
        CloseHandle(handle);
        if (FAILED(hr)) return m->Fail("D3D11 OpenSharedFence 0x%08X", unsigned(hr));
    }
    m->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m->event) return m->Fail("CreateEvent failed");

    // Depth copy shaders and a private context state, so the engine's cached D3D11 bindings
    // are untouched (same as the upscalers)
    ComPtr<ID3DBlob> vs, ps, errors;
    hr = D3DCompile(kDepthCopyHlsl, sizeof(kDepthCopyHlsl) - 1, "framegen_depth", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs, &errors);
    if (SUCCEEDED(hr)) hr = D3DCompile(kDepthCopyHlsl, sizeof(kDepthCopyHlsl) - 1, "framegen_depth", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps, &errors);
    if (FAILED(hr))
        return m->Fail("depth copy shader: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "D3DCompile failed");
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m->depthVs)) ||
        FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m->depthPs)))
        return m->Fail("depth copy shader creation failed");
    ComPtr<ID3D11Device1> device1;
    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) ||
        FAILED(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &m->state11)))
        return m->Fail("CreateDeviceContextState failed");

    m->initialized = true;
    m->Info("D3D12 device ready on %ls", adapterDesc.Description);
    return true;
}

void Bridge::Shutdown()
{
    Detach();
    if (m->queue) m->WaitIdle();
    m->depthVs.Reset(); m->depthPs.Reset(); m->state11.Reset();
    m->fenceToD3D12_11.Reset(); m->fenceToD3D11_11.Reset();
    m->fenceToD3D12.Reset(); m->fenceToD3D11.Reset();
    m->list.Reset();
    for (auto& allocator : m->allocators) allocator.Reset();
    m->queue.Reset();
    m->device12.Reset();
    m->context11.Reset(); m->device11.Reset(); m->factory.Reset();
    if (m->event) { CloseHandle(m->event); m->event = nullptr; }
    if (m->ffx) { FreeLibrary(m->ffx); m->ffx = nullptr; }
    if (m->d3d12) { FreeLibrary(m->d3d12); m->d3d12 = nullptr; }
    m->createContext = nullptr; m->destroyContext = nullptr; m->configure = nullptr; m->query = nullptr; m->dispatch = nullptr;
    m->valueToD3D12 = m->valueToD3D11 = m->lastSignalToD3D11 = 0;
    for (auto& value : m->slotValue) value = 0;
    m->initialized = false;
    if (g_messageLog == m->log) g_messageLog = nullptr;
}

bool Bridge::Attach(HWND window, unsigned width, unsigned height, DXGI_FORMAT format, bool tearing)
{
    if (!m->initialized) return m->Fail("Attach before Initialize");
    if (Attached() && m->window == window && m->width == width && m->height == height && m->format == format && m->tearing == tearing)
        return true;
    Detach();

    // AMD's frame-generation swapchain for the game window, presenting on our queue
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = {};
    fullscreen.Windowed = TRUE;

    IDXGISwapChain4* swapchain = nullptr;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 swapDesc = {};
    swapDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    swapDesc.swapchain = &swapchain;
    swapDesc.hwnd = window;
    swapDesc.desc = &desc;
    swapDesc.fullscreenDesc = &fullscreen;
    swapDesc.dxgiFactory = m->factory.Get();
    swapDesc.gameQueue = m->queue.Get();
    ffxReturnCode_t rc = m->createContext(&m->swapContext, &swapDesc.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !swapchain)
    {
        m->swapContext = nullptr;
        return m->Fail("frame generation swapchain creation failed (FidelityFX code %u)", unsigned(rc));
    }
    m->swapchain.Attach(swapchain);
    m->swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    // The engine switches screen modes itself; DXGI's own Alt+Enter would fight it
    m->factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    // FSR 3.1 frame generation context
    ffxCreateBackendDX12Desc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = m->device12.Get();
    ffxCreateContextDescFrameGeneration fg = {};
    fg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    fg.header.pNext = &backend.header;
    fg.flags = 0;   // standard depth (0 = near, finite far plane), motion vectors without jitter
    fg.displaySize = {width, height};
    fg.maxRenderSize = {width, height};
    fg.backBufferFormat = ffxApiGetSurfaceFormatDX12(format);
    rc = m->createContext(&m->fgContext, &fg.header, nullptr);
    if (rc != FFX_API_RETURN_OK)
    {
        m->fgContext = nullptr;
        Detach();
        return m->Fail("FSR 3.1 frame generation context creation failed (FidelityFX code %u)", unsigned(rc));
    }
    ffxConfigureDescGlobalDebug1 debug = {};
    debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    debug.fpMessage = FfxMessage;
    debug.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
    m->configure(&m->fgContext, &debug.header);

    m->window = window;
    m->width = width;
    m->height = height;
    m->format = format;
    m->tearing = tearing;
    if (!m->CreateShared(m->finalFrame, width, height, format, false, L"FrameGen shared frame") ||
        !m->CreateShared(m->hudless, width, height, format, false, L"FrameGen shared hudless"))
    {
        Detach();
        return false;
    }
    for (unsigned i = 0; i < kInputSets; ++i)
        if (!m->CreatePrivate(m->privHudless[i], width, height, format, L"FrameGen hudless"))
        {
            Detach();
            return false;
        }
    m->haveCapture = false;
    m->waitedThisFrame = false;
    m->frameId = 0;
    m->resetFrameId = ~0ull;

    ffxQueryGetProviderVersion version = {};
    version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    m->query(&m->fgContext, &version.header);
    m->Info("attached: %ux%u, format %u, tearing %s, provider %s", width, height, unsigned(format),
        tearing ? "yes" : "no", version.versionName ? version.versionName : "?");
    return true;
}

void Bridge::Detach()
{
    if (m->swapContext && m->dispatch)
    {
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait = {};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
        m->dispatch(&m->swapContext, &wait.header);
    }
    if (m->queue) m->WaitIdle();
    if (m->fgContext && m->destroyContext) m->destroyContext(&m->fgContext, nullptr);
    m->fgContext = nullptr;
    m->swapchain.Reset();
    if (m->swapContext && m->destroyContext) m->destroyContext(&m->swapContext, nullptr);
    m->swapContext = nullptr;
    m->ReleaseSurfaces();
    m->window = nullptr;
    m->width = m->height = 0;
    m->format = DXGI_FORMAT_UNKNOWN;
    m->haveCapture = false;
}

bool Bridge::Capture(const Inputs& in)
{
    if (!Attached()) return false;
    m->haveCapture = false;
    if (!in.hudless || !in.depth || !in.motion || !in.renderWidth || !in.renderHeight) return false;

    D3D11_TEXTURE2D_DESC hudlessDesc = {}, motionDesc = {};
    in.hudless->GetDesc(&hudlessDesc);
    in.motion->GetDesc(&motionDesc);
    if (hudlessDesc.Width != m->width || hudlessDesc.Height != m->height || hudlessDesc.Format != m->format ||
        hudlessDesc.SampleDesc.Count != 1)
        return false;
    if (motionDesc.Width != in.renderWidth || motionDesc.Height != in.renderHeight || motionDesc.SampleDesc.Count != 1)
        return false;
    if (!m->EnsureRenderInputs(in.renderWidth, in.renderHeight, motionDesc.Format)) return false;

    m->BeginWrite();
    m->context11->CopyResource(m->hudless.t11.Get(), in.hudless);
    m->context11->CopyResource(m->motion.t11.Get(), in.motion);

    // Depth: D24S8 -> R32F through a fullscreen triangle, in a private context state
    ComPtr<ID3DDeviceContextState> previous;
    m->context11->SwapDeviceContextState(m->state11.Get(), &previous);
    ID3D11RenderTargetView* rtv = m->depth.rtv11.Get();
    m->context11->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT viewport = {0, 0, float(in.renderWidth), float(in.renderHeight), 0, 1};
    m->context11->RSSetViewports(1, &viewport);
    m->context11->IASetInputLayout(nullptr);
    m->context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m->context11->VSSetShader(m->depthVs.Get(), nullptr, 0);
    m->context11->PSSetShader(m->depthPs.Get(), nullptr, 0);
    m->context11->PSSetShaderResources(0, 1, &in.depth);
    m->context11->Draw(3, 0);
    ID3D11ShaderResourceView* noSrv = nullptr;
    ID3D11RenderTargetView* noRtv = nullptr;
    m->context11->PSSetShaderResources(0, 1, &noSrv);
    m->context11->OMSetRenderTargets(1, &noRtv, nullptr);
    m->context11->SwapDeviceContextState(previous.Get(), nullptr);

    m->captured = in;
    m->haveCapture = true;
    return true;
}

HRESULT Bridge::Present(ID3D11Texture2D* backbuffer, bool vsync, bool tearing, bool generate, unsigned debugFlags)
{
    if (!Attached() || !backbuffer) return E_FAIL;
    generate = generate && m->haveCapture;

    // D3D11: the finished frame (with the UI) into the shared texture, then hand over
    m->BeginWrite();
    m->context11->CopyResource(m->finalFrame.t11.Get(), backbuffer);
    m->context11->Signal(m->fenceToD3D12_11.Get(), ++m->valueToD3D12);
    m->context11->Flush();

    // D3D12: reuse the allocator of kSlots frames ago once the queue is past it
    const unsigned slot = unsigned(m->frameIndex % kSlots);
    if (!m->WaitCpu(m->slotValue[slot])) return E_FAIL;
    m->allocators[slot]->Reset();
    m->list->Reset(m->allocators[slot].Get(), nullptr);
    m->queue->Wait(m->fenceToD3D12.Get(), m->valueToD3D12);

    const unsigned set = unsigned(m->frameIndex % kInputSets);
    ID3D12Resource* privHudless = m->privHudless[set].r12.Get();
    ID3D12Resource* privMotion = m->privMotion[set].r12.Get();
    ID3D12Resource* privDepth = m->privDepth[set].r12.Get();
    generate = generate && privHudless && privMotion && privDepth;
    if (generate)
    {
        // Shared (COMMON, promoted to COPY_SOURCE) -> private inputs FSR reads
        D3D12_RESOURCE_BARRIER toCopy[3] = {
            Transition(privHudless, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(privMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            Transition(privDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
        m->list->ResourceBarrier(3, toCopy);
        m->list->CopyResource(privHudless, m->hudless.r12.Get());
        m->list->CopyResource(privMotion, m->motion.r12.Get());
        m->list->CopyResource(privDepth, m->depth.r12.Get());
        D3D12_RESOURCE_BARRIER toRead[3] = {
            Transition(privHudless, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(privMotion, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(privDepth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
        m->list->ResourceBarrier(3, toRead);
    }

    // Per-frame configuration: on/off, the hudless image for UI detection, our dispatch callback
    const UINT64 frameId = ++m->frameId;
    if (generate && m->captured.reset) m->resetFrameId = frameId;
    ffxConfigureDescFrameGeneration config = {};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = m->swapchain.Get();
    config.frameGenerationEnabled = generate;
    config.allowAsyncWorkloads = false;   // generation runs on our queue, ordered with the copies above
    config.frameGenerationCallback = &Impl::DispatchFrameGeneration;
    config.frameGenerationCallbackUserContext = m.get();
    if (generate) config.HUDLessColor = ffxApiGetResourceDX12(privHudless, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    config.flags = debugFlags;
    config.onlyPresentGenerated = false;
    config.generationRect = {0, 0, int32_t(m->width), int32_t(m->height)};
    config.frameID = frameId;
    ffxReturnCode_t rc = m->configure(&m->fgContext, &config.header);
    if (rc != FFX_API_RETURN_OK && generate)
    {
        m->Fail("frame generation configure failed (FidelityFX code %u); this frame is presented without it", unsigned(rc));
        generate = false;
        config.frameGenerationEnabled = false;
        config.HUDLessColor = {};
        m->configure(&m->fgContext, &config.header);
    }

    if (generate)
    {
        const Inputs& in = m->captured;
        ffxDispatchDescFrameGenerationPrepareCameraInfo camera = {};
        camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        for (int i = 0; i < 3; ++i)
        {
            camera.cameraPosition[i] = in.position[i];
            camera.cameraUp[i] = in.up[i];
            camera.cameraRight[i] = in.right[i];
            camera.cameraForward[i] = in.forward[i];
        }
        ffxDispatchDescFrameGenerationPrepare prepare = {};
        prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        prepare.header.pNext = &camera.header;
        prepare.frameID = frameId;
        prepare.flags = debugFlags;
        prepare.commandList = m->list.Get();
        prepare.renderSize = {in.renderWidth, in.renderHeight};
        prepare.jitterOffset = {in.jitterX, in.jitterY};
        prepare.motionVectorScale = {in.motionScaleX, in.motionScaleY};
        prepare.frameTimeDelta = in.frameTimeMs;
        prepare.cameraNear = in.cameraNear;
        prepare.cameraFar = in.cameraFar;
        prepare.cameraFovAngleVertical = in.fovY;
        prepare.viewSpaceToMetersFactor = 1.0f;
        prepare.depth = ffxApiGetResourceDX12(privDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        prepare.motionVectors = ffxApiGetResourceDX12(privMotion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        rc = m->dispatch(&m->fgContext, &prepare.header);
        if (rc != FFX_API_RETURN_OK) m->Fail("frame generation prepare failed (FidelityFX code %u)", unsigned(rc));
    }

    // The finished frame into AMD's swapchain buffer
    ComPtr<ID3D12Resource> target;
    HRESULT hr = m->swapchain->GetBuffer(m->swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&target));
    if (SUCCEEDED(hr))
    {
        D3D12_RESOURCE_BARRIER toCopy = Transition(target.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        m->list->ResourceBarrier(1, &toCopy);
        m->list->CopyResource(target.Get(), m->finalFrame.r12.Get());
        D3D12_RESOURCE_BARRIER toPresent = Transition(target.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        m->list->ResourceBarrier(1, &toPresent);
    }
    m->list->Close();
    ID3D12CommandList* lists[] = {m->list.Get()};
    m->queue->ExecuteCommandLists(1, lists);
    target.Reset();

    if (SUCCEEDED(hr))
    {
        const UINT flags = (tearing && !vsync && m->tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        hr = m->swapchain->Present(vsync ? 1 : 0, flags);
        if (FAILED(hr)) m->Fail("Present 0x%08X", unsigned(hr));
    }
    else
        m->Fail("swapchain GetBuffer 0x%08X", unsigned(hr));

    // Everything above (including the generation AMD's swapchain queued on our queue) is
    // ordered before this signal: the next frame's D3D11 writes wait for it
    m->queue->Signal(m->fenceToD3D11.Get(), ++m->valueToD3D11);
    m->slotValue[slot] = m->valueToD3D11;
    m->lastSignalToD3D11 = m->valueToD3D11;
    ++m->frameIndex;
    m->haveCapture = false;
    m->waitedThisFrame = false;
    return hr;
}
}
