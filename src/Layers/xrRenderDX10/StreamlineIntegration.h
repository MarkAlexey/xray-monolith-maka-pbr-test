// Adapted from OGSR; credits: OGSR team and xrSimpAI.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <string>
#include "../../../sdk/Streamline/include/sl.h"
#include "../../../sdk/Streamline/include/sl_dlss_g.h"
#include "../../../sdk/Streamline/include/sl_reflex.h"
#include "../../../sdk/Streamline/include/sl_pcl.h"

// One runtime, one viewport (0), one frame token shared by all latency markers and inputs.
// No static interposer dependency; ordinary DX11 and bridge-only launches never load SL.
class StreamlineIntegration final
{
public:
    using Logger = void (*)(const char*);
    // Process-wide. Streamline must not be initialized again after slShutdown in the
    // same process: DLSS-G (nvngx_dlssg) keeps a callback into the unloaded plugin and
    // faults on a later init. Once shut down, it is RestartRequired.
    enum class Support { Unknown, Supported, Unsupported, RestartRequired };
    static Support FrameGenerationSupport();
    static bool CanInitialize(); // False once slInit has been reached in this process.
    ~StreamlineIntegration();
    bool Initialize(const wchar_t* directory, Logger logger);
    bool CheckAdapter(const LUID& luid);
    bool SetDevice(ID3D12Device* device);
    bool Upgrade(void** object);
    bool BeginFrame(UINT frameLimitUs);
    void Marker(sl::PCLMarker marker);
    bool SetEnabled(bool enabled);
    bool Tag(const sl::Constants& constants, ID3D12Resource* depth, ID3D12Resource* motion,
        ID3D12Resource* hudless);
    bool ClearTags();
    // Called on the Present thread; returns an AddRef'd fence so it survives mode
    // changes. Caller retires input resources on its own timeline and releases it.
    bool AfterPresent(ID3D12Fence** inputFence, UINT64& inputFenceValue);
    // Recovery only: caller MUST drain the presenting queue before calling.
    bool DisableAfterFailure();
    void Shutdown();
    void Unload(); // Only AFTER all Streamline proxy COM interfaces have been released.
    bool IsReady() const { return m_deviceReady; }
    bool IsEnabled() const { return m_enabled; }
    bool HasFrame() const { return m_frame != nullptr; }
    UINT PresentedFrames() const { return m_presentedFrames; }
    bool CanGenerate(UINT width, UINT height) const { return !m_failed && width >= m_minDimension && height >= m_minDimension; }
    // After DisableAfterFailure: allow DLSS-G again and resend options on the next switch.
    void ClearFailure() { m_failed = false; m_optionsSet = false; }
    bool SupportsVSync() const { return m_supportsVSync; }
    const char* FailureStage() const { return m_failureStage; }

private:
    bool Check(sl::Result result, const char* stage);
    void Log(const char* message) const;
    HMODULE m_module{};
    Logger m_logger{};
    bool m_initialized{}, m_deviceReady{}, m_enabled{}, m_optionsSet{};
    bool m_failed{}, m_supportsVSync{};
    bool m_vramWarningLogged{};
    UINT m_minDimension{};
    sl::FrameToken* m_frame{};
    UINT m_frameLimitUs{~0u}, m_presentedFrames{};
    UINT m_markerMask{}, m_frameNumber{}, m_tagFrame{}, m_presentNumber{};
    UINT m_diagnosticPresents{}; // Bounded snapshots of existing GetState results after mode changes.
    UINT m_missingFenceWarnings{}, m_markerGaps{};
    std::wstring m_directory;
    const char* m_failureStage{"none"};
    sl::ViewportHandle m_viewport{0};
#define SL_ENTRY(name) PFun_##name* m_##name{};
    SL_ENTRY(slInit)
    SL_ENTRY(slShutdown)
    SL_ENTRY(slIsFeatureSupported)
    SL_ENTRY(slSetD3DDevice)
    SL_ENTRY(slUpgradeInterface)
    SL_ENTRY(slGetFeatureFunction)
    SL_ENTRY(slGetNewFrameToken)
    SL_ENTRY(slSetConstants)
    SL_ENTRY(slSetTagForFrame)
    SL_ENTRY(slDLSSGSetOptions)
    SL_ENTRY(slDLSSGGetState)
    SL_ENTRY(slReflexSetOptions)
    SL_ENTRY(slReflexSleep)
    SL_ENTRY(slPCLSetMarker)
    SL_ENTRY(slPCLSetOptions)
#undef SL_ENTRY
};
