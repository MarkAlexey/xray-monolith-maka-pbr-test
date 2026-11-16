// Adapted from OGSR; credits: OGSR team and xrSimpAI.
#pragma once

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include "StreamlineIntegration.h"

// FSR SDK 2 headers stay out of this header: the DX11 FSR3 path includes the
// SDK 1.x headers, which define some of the same type names.
class FfxApi;
struct ffxDispatchDescFrameGeneration;

// Who owns the DX12 swapchain. Fixed for the bridge's lifetime; switching rebuilds it.
// Streamline: proxy swapchain with Reflex, optional DLSS-G (also used for FG Off).
// Fsr: AMD FSR frame-generation swapchain. Plain: no runtime (fallback).
enum class PresentRuntime { Plain, Streamline, Fsr };

// API-neutral per-frame data for frame generation, filled by the renderer.
// Matrices are row-major for row vectors (D3DX/X-Ray layout).
struct FrameGenerationInputs
{
    float viewToClip[16]{}, clipToView[16]{}, clipToPrevClip[16]{}, prevClipToClip[16]{};
    float jitterX{}, jitterY{};             // Pixels, as used for the scene raster.
    float motionToUvX{}, motionToUvY{};     // Scale from stored vectors to current->previous UV.
    float cameraPosition[3]{}, cameraUp[3]{}, cameraRight[3]{}, cameraForward[3]{};
    float cameraNear{}, cameraFar{}, verticalFov{}, aspectRatio{}, frameTimeDelta{};
    bool reset{};
};

struct UpscaleInputs
{
    float jitterX{}, jitterY{};         // Pixels.
    float motionScaleX{}, motionScaleY{}; // To current->previous pixels at render size.
    float frameTimeDelta{}, cameraNear{}, cameraFar{}, verticalFov{};
    bool reset{};
};

// Native D3D11 rendering + native D3D12 presentation on the same adapter.
// All methods are called on the render/present thread using the immediate context.
class D3D12PresentBridge final
{
public:
    using Logger = StreamlineIntegration::Logger;
    D3D12PresentBridge();
    ~D3D12PresentBridge();
    D3D12PresentBridge(const D3D12PresentBridge&) = delete;
    D3D12PresentBridge& operator=(const D3D12PresentBridge&) = delete;

    HRESULT Initialize(IDXGIFactory2* factory, IDXGIAdapter* adapter, ID3D11Device* device11,
        ID3D11DeviceContext* context11, HWND window, const DXGI_SWAP_CHAIN_DESC1& desc, bool debug,
        PresentRuntime runtime, Logger logger = nullptr, bool profiling = false);
    HRESULT Resize(UINT width, UINT height, DXGI_FORMAT format, UINT flags);
    HRESULT Present(UINT interval, UINT flags);
    HRESULT GetRenderTargetView(ID3D11RenderTargetView** view) const;
    HRESULT FailGeneration(const char* reason);
    bool GenerationFailed() const { return m_fgFailed; }
    // After an automatic disable, let FG start again when the player re-enables it.
    void ClearGenerationFailure();
    // Acquire BEFORE recording/executing any frame commands that touch the shared targets.
    // Independent of the proxy's backbuffer index, which can change when FG is toggled.
    HRESULT AcquireFrame();
    HRESULT SubmitFrame(); // One color copy: shared D3D11 target -> D3D12 backbuffer.
    HRESULT WaitIdle();
    HRESULT AfterPresent();

    PresentRuntime Runtime() const { return m_runtimeKind; }
    const char* RuntimeName() const;
    // A frame-generation runtime is loaded and has not failed.
    bool FrameGenerationReady() const;
    // The last presented frame was submitted with generation enabled.
    bool GenerationActive() const { return m_lastGenerated; }
    bool VSyncBlocksGeneration() const { return m_vsyncBlocked; }
    StreamlineIntegration* GetStreamline() { return m_streamline.IsReady() ? &m_streamline : nullptr; }
    HRESULT BeginFrame(UINT frameLimitUs, bool allowGeneration);
    HRESULT CaptureScene(ID3D11Resource* depth, ID3D11Resource* motion, const FrameGenerationInputs& inputs);
    HRESULT CaptureHudless(bool valid);
    bool NeedsFGInputs() const;
    void ResetHistory() { m_resetHistory = true; }
    HRESULT SuspendGeneration();
    void Shutdown();

    // DX12 AMD FSR upscaling (FSR 4 on supported Radeon GPUs, runtime fallback elsewhere).
    // Inputs are copied/converted into shared textures; the DX11 queue waits for the
    // upscaled output on the GPU. The output texture is stable until DestroyUpscaler.
    HRESULT CreateUpscaler(UINT renderWidth, UINT renderHeight, UINT displayWidth, UINT displayHeight, bool hdr);
    void DestroyUpscaler();
    HRESULT Upscale(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* motion, const UpscaleInputs& inputs);
    ID3D11Texture2D* UpscaleOutput() const { return m_upOutput11.Get(); }
    const char* UpscalerVersion() const { return m_upVersion.c_str(); }
    bool UpscalerReady() const { return m_upContext != nullptr; }
    // AMD FSR runtime present (signed loader beside the exe) and the versions it would
    // select on this GPU, e.g. "4.1.1" or "3.1.5"; empty when unavailable. Cached.
    bool FsrRuntimeAvailable();
    const char* FsrUpscalerVersion();
    const char* FsrFrameGenerationVersion();
    const char* LastFailure() const { return m_failureStage ? m_failureStage : "none"; }

    // Stable per-slot native D3D11 resources; cache views at creation, select at frame start.
    HRESULT GetRenderSurface(ID3D11Texture2D** surface) const;
    HRESULT GetRenderSurface(UINT slot, ID3D11Texture2D** surface) const;
    UINT SurfaceCount() const { return UINT(m_slots.size()); }
    UINT CurrentSlot() const { return m_currentSlot; }
    ID3D12Resource* GetCapturedInput(UINT input) const; // Diagnostics, valid until slot reuse/resize.
    struct Statistics
    {
        UINT64 frames{}, sceneCaptures{}, hudlessCaptures{}, inputRebuilds{};
        UINT64 allocatorWaits{}, reuseWaits{}, allocatorWaitTicks{}, maxAllocatorWaitTicks{};
        UINT64 copySamples{}, copyTicks{}, colorBytes{}, inputCopyBytes{};
        UINT64 upscales{};
    };
    const Statistics& GetStatistics() const { return m_statistics; }
    IDXGISwapChain3* GetSwapChain() const { return m_swapChain.Get(); }
    ID3D12Device* GetDevice() const { return m_device12.Get(); }
    ID3D12CommandQueue* GetQueue() const { return m_queue.Get(); }
    const char* FailureStage() const { return m_failureStage; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Slot
    {
        ComPtr<ID3D12Resource> shared12;
        ComPtr<ID3D11Texture2D> shared11;
        ComPtr<ID3D11RenderTargetView> view11;
        ComPtr<ID3D12Resource> backBuffer;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        UINT64 finished{};
        UINT64 retired{};
        // DLSS-G's "inputs processed" fence for this slot's last generated frame.
        // Checked on the CPU before DX11 rewrites the inputs; never waited on by
        // the presenting queue (see AfterPresent).
        ComPtr<ID3D12Fence> inputFence;
        UINT64 inputValue{};
        bool timestampPending{};
        ComPtr<ID3D12Resource> inputs12[3]; // depth, motion, HUD-less color
        ComPtr<ID3D11Texture2D> inputs11[3];
        ComPtr<ID3D11UnorderedAccessView> depthUAV, motionUAV;
    };

    HRESULT CreateSurfaces();
    HRESULT CreateSharedFence(ComPtr<ID3D12Fence>& fence12, ComPtr<ID3D11Fence>& fence11);
    HRESULT CreateSharedTexture(UINT width, UINT height, DXGI_FORMAT format, bool unorderedAccess,
        ComPtr<ID3D12Resource>& texture12, ComPtr<ID3D11Texture2D>& texture11);
    HRESULT WaitFor(ID3D12Fence* fence, UINT64 value, bool allocatorReuse = false);
    HRESULT Check(HRESULT result, const char* stage);
    // Once per bridge: fence progress and device-removed reasons after a timeout or device loss.
    void ReportStall(const char* stage, HRESULT result);
    HRESULT CreateInputs(UINT width, UINT height, DXGI_FORMAT motionFormat);
    // Converts engine depth (D24S8/R32) and SSS motion (RGBA16F) to R32F/RG16F UAVs in a
    // private context state; optionally copies one more resource there (unbound source).
    HRESULT ConvertDepthMotion(ID3D11Texture2D* depth, ID3D11Texture2D* motion,
        ID3D11UnorderedAccessView* depthOut, ID3D11UnorderedAccessView* motionOut, UINT width, UINT height, bool sample,
        ID3D11Resource* copyTarget = nullptr, ID3D11Resource* copySource = nullptr);
    HRESULT SampleInputs();
    void LogDeviceCapabilities();
    void SampleDisplayedFrames();

    // AMD FSR frame generation (Fsr runtime only).
    HRESULT CreateFsrSwapChain(IDXGIFactory2* factory, HWND window, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen,
        ComPtr<IDXGISwapChain1>& swapChain);
    HRESULT CreateFsrFrameGeneration();
    void DestroyFsrFrameGeneration();
    HRESULT ConfigureFsr(bool generate, Slot& slot);
    static uint32_t FsrGenerateCallback(ffxDispatchDescFrameGeneration* params, void* user);
    HRESULT SetGeneration(bool enabled);
    HRESULT TagStreamline(Slot& slot);

    HMODULE m_runtime{};
    PresentRuntime m_runtimeKind{PresentRuntime::Plain};
    StreamlineIntegration m_streamline;
    std::unique_ptr<FfxApi> m_ffx;
    void* m_fsrSwapChainContext{}; // ffxContext
    void* m_fsrFrameGeneration{};  // ffxContext
    UINT64 m_fsrFrameId{};
    std::string m_fsrFgVersion;
    ComPtr<ID3D12Device> m_proxyDevice;
    HANDLE m_fenceEvent{};
    ComPtr<ID3D11Device5> m_device11;
    ComPtr<ID3D11DeviceContext4> m_context11;
    ComPtr<ID3D12Device> m_device12;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<IDXGIAdapter3> m_adapter;
    // Separate monotonic timelines: DX11 ready, presentation copy done, FG inputs retired.
    ComPtr<ID3D12Fence> m_ready12, m_finished12;
    ComPtr<ID3D11Fence> m_ready11, m_finished11;
    ComPtr<ID3D12Fence> m_retired12;
    ComPtr<ID3D11Fence> m_retired11;
    std::vector<Slot> m_slots;
    DXGI_SWAP_CHAIN_DESC1 m_desc{};
    UINT64 m_serial{};
    UINT64 m_lastReady{}, m_lastRetired{}; // last values signalled, for ReportStall
    bool m_stallReported{};
    UINT m_currentSlot{}, m_nextSlot{};
    bool m_acquired{}, m_submitted{}, m_submittedFG{}, m_lastGenerated{}, m_vsyncBlocked{};
    UINT m_inputWidth{}, m_inputHeight{};
    DXGI_FORMAT m_motionFormat{DXGI_FORMAT_UNKNOWN};
    ComPtr<ID3D11Texture2D> m_depthSource;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;
    ComPtr<ID3D11ComputeShader> m_depthShader, m_diagnosticShader;
    ComPtr<ID3DDeviceContextState> m_captureState;
    ComPtr<ID3D11ShaderResourceView> m_motionSRV;
    ComPtr<ID3D11Texture2D> m_motionSource, m_depthCopy;
    ComPtr<ID3D11Buffer> m_diagnosticBuffer, m_diagnosticReadback;
    ComPtr<ID3D11UnorderedAccessView> m_diagnosticUAV;
    bool m_diagnosticPending{}, m_fgFailed{};
    UINT64 m_diagnosticFrame{};
    FrameGenerationInputs m_inputs{};
    bool m_allowGeneration{}, m_sceneValid{}, m_hudlessValid{}, m_resetHistory{true};
    Statistics m_statistics{};
    bool m_profiling{};
    UINT64 m_generatedPresents{}, m_enabledPresents{}, m_totalPresented{}, m_markerFrames{};
    UINT64 m_displayPresentBase{}, m_displayFrameBase{};
    Logger m_logger{};
    ComPtr<ID3D12QueryHeap> m_timestampHeap;
    ComPtr<ID3D12Resource> m_timestampReadback;
    UINT64 m_timestampFrequency{};
    void ReportStatistics();
    const char* m_failureStage{"none"};

    // DX12 upscaler: own direct queue so it never waits behind frame generation.
    static constexpr UINT UpscaleAllocators = 3;
    void* m_upContext{}; // ffxContext
    int m_fsrAvailable{-1};
    std::string m_fsrUpscalerDefault, m_fsrFrameGenerationDefault;
    std::string FsrDefaultVersion(uint64_t createDescType);
    std::string m_upVersion;
    bool m_upHdr{}, m_upNonLinear{}, m_upNonLinearDispatch{};
    UINT m_upRenderWidth{}, m_upRenderHeight{}, m_upDisplayWidth{}, m_upDisplayHeight{};
    ComPtr<ID3D12CommandQueue> m_upQueue;
    ComPtr<ID3D12CommandAllocator> m_upAllocators[UpscaleAllocators];
    UINT64 m_upAllocatorFence[UpscaleAllocators]{};
    ComPtr<ID3D12GraphicsCommandList> m_upCommands;
    ComPtr<ID3D12Fence> m_upReady12, m_upDone12;
    ComPtr<ID3D11Fence> m_upReady11, m_upDone11;
    UINT64 m_upSerial{};
    ComPtr<ID3D12Resource> m_upColor12, m_upDepth12, m_upMotion12, m_upOutput12;
    ComPtr<ID3D11Texture2D> m_upColor11, m_upDepth11, m_upMotion11, m_upOutput11;
    ComPtr<ID3D11UnorderedAccessView> m_upDepthUAV, m_upMotionUAV;
    DXGI_FORMAT m_upColorFormat{DXGI_FORMAT_UNKNOWN};
};
