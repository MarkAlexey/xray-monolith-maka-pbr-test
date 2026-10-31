// Adapted from OGSR; credits: OGSR team and xrSimpAI.
#include "StreamlineIntegration.h"
#include "../../../sdk/Streamline/include/sl_security.h"
#include <cstdio>
#include <atomic>

namespace
{
std::atomic<StreamlineIntegration::Logger> s_logger{};
std::atomic<StreamlineIntegration::Support> s_support{StreamlineIntegration::Support::Unknown};
std::atomic<bool> s_attempted{};
void StreamlineLog(sl::LogType type, const char* message)
{
    if (type == sl::LogType::eInfo) return; // Full details remain in sl.log.
    if (auto logger = s_logger.load()) logger(message);
}
}

StreamlineIntegration::~StreamlineIntegration() { Shutdown(); Unload(); }

StreamlineIntegration::Support StreamlineIntegration::FrameGenerationSupport() { return s_support.load(); }
bool StreamlineIntegration::CanInitialize() { return !s_attempted.load(); }

void StreamlineIntegration::Log(const char* message) const
{
    if (m_logger) m_logger(message);
}

bool StreamlineIntegration::Check(sl::Result result, const char* stage)
{
    if (result == sl::Result::eOk) return true;
    m_failureStage = stage;
    char message[256]{};
    snprintf(message, sizeof(message), "[DLSS FG] %s failed: Streamline result %u", stage, unsigned(result));
    Log(message);
    return false;
}

bool StreamlineIntegration::Initialize(const wchar_t* directory, Logger logger)
{
    if (m_module || !directory) return false;
    m_logger = logger;
    s_logger.store(logger);
    if (s_attempted.load())
    {
        m_failureStage = "Streamline already ran in this session (re-initialization is unsafe; restart the game for DLSS frame generation/Reflex)";
        return false;
    }
    m_directory = directory;
    const auto interposer = m_directory + L"\\sl.interposer.dll";
    if (!sl::security::verifyEmbeddedSignature(interposer.c_str()))
    {
        m_failureStage = "NVIDIA interposer signature verification";
        Log("[DLSS FG] Missing/untrusted sl.interposer.dll; FG disabled.");
        return false;
    }
    m_module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m_module)
    {
        m_failureStage = "Load signed interposer";
        return false;
    }
#define LOAD_ENTRY(name) m_##name = reinterpret_cast<PFun_##name*>(GetProcAddress(m_module, #name)); \
    if (!m_##name) { m_failureStage = #name " export"; return false; }
    LOAD_ENTRY(slInit)
    LOAD_ENTRY(slShutdown)
    LOAD_ENTRY(slIsFeatureSupported)
    LOAD_ENTRY(slSetD3DDevice)
    LOAD_ENTRY(slUpgradeInterface)
    LOAD_ENTRY(slGetFeatureFunction)
    LOAD_ENTRY(slGetNewFrameToken)
    LOAD_ENTRY(slSetConstants)
    LOAD_ENTRY(slSetTagForFrame)
#undef LOAD_ENTRY
    const sl::Feature features[]{sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    const wchar_t* paths[]{m_directory.c_str()};
    sl::Preferences preferences{};
    preferences.logMessageCallback = StreamlineLog;
    preferences.pathToLogsAndData = m_directory.c_str();
    preferences.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eDisableCLStateTracking |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = UINT(std::size(features));
    preferences.pathsToPlugins = paths;
    preferences.numPathsToPlugins = 1;
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    // Streamline's applicationId is 32-bit; never truncate the existing 64-bit NGX SR ID.
    // Use the supported custom-engine/project identity path, not an invented NVIDIA app ID.
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "xray-monolith-DX12-1";
    preferences.projectId = "6f27fb13-3c63-4b18-8f66-503897a6aabe";
    // From here on plugins load; even a failed slInit must not be retried in-process.
    s_attempted.store(true);
    if (!Check(m_slInit(preferences, sl::kSDKVersion), "slInit")) return false;
    m_initialized = true;
    char message[160]{};
    // The SDK version compiled in; sl.log records the runtime plugin versions.
    snprintf(message, sizeof(message), "[DLSS FG] Streamline initialized, SDK %d.%d.%d (manual DX12 hooks; native DX11 SR unchanged).",
        SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
    Log(message);
    return true;
}

bool StreamlineIntegration::SetDevice(ID3D12Device* device)
{
    if (!Check(m_slSetD3DDevice(device), "slSetD3DDevice")) return false;
#define LOAD_FEATURE(feature, name) { void* address{}; \
    if (!Check(m_slGetFeatureFunction(feature, #name, address), #name " import")) return false; \
    m_##name = reinterpret_cast<PFun_##name*>(address); if (!m_##name) return false; }
    LOAD_FEATURE(sl::kFeatureDLSS_G, slDLSSGSetOptions)
    LOAD_FEATURE(sl::kFeatureDLSS_G, slDLSSGGetState)
    LOAD_FEATURE(sl::kFeatureReflex, slReflexSetOptions)
    LOAD_FEATURE(sl::kFeatureReflex, slReflexSleep)
    LOAD_FEATURE(sl::kFeaturePCL, slPCLSetMarker)
    LOAD_FEATURE(sl::kFeaturePCL, slPCLSetOptions)
#undef LOAD_FEATURE
    sl::PCLOptions options{};
    options.idThread = GetCurrentThreadId();
    if (!Check(m_slPCLSetOptions(options), "PCL options")) return false;
    m_deviceReady = true;
    sl::DLSSGState state{};
    if (!Check(m_slDLSSGGetState(m_viewport, state, nullptr), "FG capabilities")) return false;
    m_minDimension = state.minWidthOrHeight;
    m_supportsVSync = state.bIsVsyncSupportAvailable == sl::eTrue;
    return true; // Default is Off; set the first options with the first presented frame.
}

bool StreamlineIntegration::CheckAdapter(const LUID& luid)
{
    sl::AdapterInfo info{};
    info.deviceLUID = reinterpret_cast<uint8_t*>(const_cast<LUID*>(&luid));
    info.deviceLUIDSizeInBytes = sizeof(luid);
    const bool supported = Check(m_slIsFeatureSupported(sl::kFeatureDLSS_G, info), "DLSS-G adapter support") &&
        Check(m_slIsFeatureSupported(sl::kFeatureReflex, info), "Reflex adapter support") &&
        Check(m_slIsFeatureSupported(sl::kFeaturePCL, info), "PCL support");
    s_support.store(supported ? Support::Supported : Support::Unsupported);
    return supported;
}

bool StreamlineIntegration::Upgrade(void** object)
{
    return Check(m_slUpgradeInterface(object), "slUpgradeInterface");
}

bool StreamlineIntegration::BeginFrame(UINT frameLimitUs)
{
    m_frame = nullptr;
    m_markerMask = 0;
    ++m_frameNumber;
    if (!m_deviceReady) return false;
    if (frameLimitUs != m_frameLimitUs)
    {
        sl::ReflexOptions options{};
        options.mode = sl::ReflexMode::eLowLatency;
        options.frameLimitUs = frameLimitUs;
        if (!Check(m_slReflexSetOptions(options), "Reflex options")) return false;
        m_frameLimitUs = frameLimitUs;
    }
    if (!Check(m_slGetNewFrameToken(m_frame, nullptr), "Frame token")) return false;
    if (!Check(m_slReflexSleep(*m_frame), "Reflex sleep")) { m_frame = nullptr; return false; }
    Marker(sl::PCLMarker::eSimulationStart);
    return true;
}

void StreamlineIntegration::Marker(sl::PCLMarker marker)
{
    if (!m_frame) return;
    UINT bit = 0;
    switch (marker) {
    case sl::PCLMarker::eSimulationStart: bit=1; break;
    case sl::PCLMarker::eSimulationEnd: bit=2; break;
    case sl::PCLMarker::eRenderSubmitStart: bit=4; break;
    case sl::PCLMarker::eRenderSubmitEnd: bit=8; break;
    case sl::PCLMarker::ePresentStart: bit=16; break;
    case sl::PCLMarker::ePresentEnd: bit=32; break;
    default: break;
    }
    if (Check(m_slPCLSetMarker(marker, *m_frame), "PCL marker")) m_markerMask |= bit;
}

bool StreamlineIntegration::SetEnabled(bool enabled)
{
    if (!m_deviceReady) return false;
    enabled = enabled && !m_failed;
    if (m_optionsSet && m_enabled == enabled) return true;
    sl::DLSSGOptions options{};
    options.mode = enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = 1;
    // Normal menu/toggle transitions retain resources. A failed feature instead
    // releases its internal allocations when Off takes effect at the next Present.
    options.flags = m_failed ? sl::DLSSGFlags{} : sl::DLSSGFlags::eRetainResourcesWhenOff;
    options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    if (!Check(m_slDLSSGSetOptions(m_viewport, options), "DLSS-G options")) return false;
    char diagnostic[192]{};
    snprintf(diagnostic, sizeof(diagnostic), "[FG DIAG] SetOptions: enabled=%u retain=%u failed=%u; sampling next 120 post-Present states (no extra queries).",
        unsigned(enabled), unsigned(!m_failed), unsigned(m_failed));
    Log(diagnostic);
    m_diagnosticPresents = 120;
    if (enabled != m_enabled) Log(enabled ? "[DLSS FG] Interpolation enabled (2x)." : "[DLSS FG] Interpolation suspended.");
    m_enabled = enabled;
    m_optionsSet = true;
    return true;
}

bool StreamlineIntegration::Tag(const sl::Constants& constants, ID3D12Resource* depth, ID3D12Resource* motion,
    ID3D12Resource* hudless)
{
    if (!m_frame || !depth || !motion || !hudless) return false;
    m_tagFrame = m_frameNumber;
    if (m_diagnosticPresents == 120 || m_diagnosticPresents == 61)
    {
        const auto d=depth->GetDesc(), m=motion->GetDesc(), h=hudless->GetDesc();
        char message[512]{};
        snprintf(message,sizeof(message),"[FG DIAG] Tags token=%u depth=%llux%u fmt=%u motion=%llux%u fmt=%u HUDless=%llux%u fmt=%u scale=(%.1f,%.1f) jitter=(%.4f,%.4f) near/far=(%.4f,%.1f) reset=%u",
            m_frameNumber,d.Width,d.Height,unsigned(d.Format),m.Width,m.Height,unsigned(m.Format),h.Width,h.Height,unsigned(h.Format),
            constants.mvecScale.x,constants.mvecScale.y,constants.jitterOffset.x,constants.jitterOffset.y,constants.cameraNear,constants.cameraFar,unsigned(constants.reset));
        Log(message);
    }
    if (!Check(m_slSetConstants(constants, *m_frame, m_viewport), "Frame constants")) return false;
    sl::Resource resources[]{
        {sl::ResourceType::eTex2d, depth, nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON},
        {sl::ResourceType::eTex2d, motion, nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON},
        {sl::ResourceType::eTex2d, hudless, nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON}};
    sl::ResourceTag tags[]{
        {&resources[0], sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent},
        {&resources[1], sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent},
        {&resources[2], sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent}};
    const auto colorDesc = hudless->GetDesc();
    for (UINT i = 0; i < 3; ++i)
    {
        const auto desc = static_cast<ID3D12Resource*>(resources[i].native)->GetDesc();
        tags[i].extent = {0, 0, UINT(desc.Width), desc.Height};
    }
    tags[3].extent = {0, 0, UINT(colorDesc.Width), colorDesc.Height};
    return Check(m_slSetTagForFrame(*m_frame, m_viewport, tags, UINT(std::size(tags)), nullptr), "FG input tags");
}

bool StreamlineIntegration::ClearTags()
{
    if (!m_frame) return true;
    sl::ResourceTag tags[]{
        {nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent}};
    return Check(m_slSetTagForFrame(*m_frame, m_viewport, tags, UINT(std::size(tags)), nullptr), "Clear FG tags");
}

bool StreamlineIntegration::AfterPresent(ID3D12Fence** inputFence, UINT64& inputFenceValue)
{
    if (!inputFence) return false;
    *inputFence = nullptr;
    inputFenceValue = 0;
    m_presentedFrames = 0;
    // Do not keep querying a failed FG plugin after successful recovery. Reflex
    // and ordinary proxy presentation remain active; FG statistics are unavailable.
    if (m_failed && !m_enabled) return true;
    sl::DLSSGState state{};
    // A warning is not a failed Present. Still require usable output: alternative
    // runtimes may return a warning without filling the state/fence fields.
    state.status = static_cast<sl::DLSSGStatus>(~0u);
    state.numFramesActuallyPresented = ~0u;
    state.lastPresentInputsProcessingCompletionFenceValue = ~UINT64(0);
    const auto result = m_slDLSSGGetState(m_viewport, state, nullptr);
    // Observe the normal query only: no extra SDK calls, waits, or recovery changes.
    if (m_diagnosticPresents)
    {
        const UINT sample = 121 - m_diagnosticPresents--;
        if (sample == 1 || sample == 2 || sample == 60 || sample == 120)
        {
            char diagnostic[320]{};
            snprintf(diagnostic, sizeof(diagnostic), "[FG DIAG] PostPresent sample=%u enabled=%u result=%u status=0x%X presented=%u inputFence=%p value=%llu (all-ones fields may be unwritten).",
                sample, unsigned(m_enabled), unsigned(result), unsigned(state.status), state.numFramesActuallyPresented,
                static_cast<void*>(state.inputsProcessingCompletionFence),
                static_cast<unsigned long long>(state.lastPresentInputsProcessingCompletionFenceValue));
            Log(diagnostic);
        }
    }
    if (result == sl::Result::eWarnOutOfVRAM)
    {
        if (state.status == static_cast<sl::DLSSGStatus>(~0u) || state.numFramesActuallyPresented == ~0u ||
            state.lastPresentInputsProcessingCompletionFenceValue == ~UINT64(0) ||
            (m_enabled && (!state.inputsProcessingCompletionFence || !state.lastPresentInputsProcessingCompletionFenceValue)))
            return Check(sl::Result::eErrorInvalidState, "DLSS-G VRAM warning without usable state/input fence");
        if (!m_vramWarningLogged)
        {
            Log("[DLSS FG] VRAM pressure warning (eWarnOutOfVRAM); processing returned state and input fence. Further VRAM warnings suppressed.");
            m_vramWarningLogged = true;
        }
    }
    else if (!Check(result, "DLSS-G state")) return false;
    ++m_presentNumber;
    const bool markerGap = m_enabled && (m_markerMask != 63 || m_tagFrame != m_frameNumber);
    if (markerGap) ++m_markerGaps;
    if (m_presentNumber % 240 == 0 || (markerGap && m_markerGaps <= 8))
    {
        char message[256]{};
        snprintf(message,sizeof(message),"[FG DIAG] Frame token=%u tagged=%u markerMask=0x%02X (expected 0x3F) enabled=%u actualPresented=%u status=0x%X",
            m_frameNumber,m_tagFrame,m_markerMask,unsigned(m_enabled),state.numFramesActuallyPresented,unsigned(state.status));
        Log(message);
    }
    // Diagnostics only. Marker gaps affect latency stats, not image safety, and a
    // missing input fence still retires inputs in presenting-queue order
    // (eBlockPresentingClientQueue). Neither may disable FG on its own.
    const bool missingFence = !state.inputsProcessingCompletionFence ||
        !state.lastPresentInputsProcessingCompletionFenceValue ||
        state.lastPresentInputsProcessingCompletionFenceValue == ~UINT64(0);
    if (m_enabled && (missingFence || state.numFramesActuallyPresented == ~0u) &&
        (m_missingFenceWarnings < 8 || m_presentNumber % 240 == 0))
    {
        ++m_missingFenceWarnings;
        char message[192]{};
        snprintf(message, sizeof(message), "[FG DIAG] Present %u: no input completion fence/present count from DLSS-G (warning %u); retiring inputs in queue order.",
            m_presentNumber, m_missingFenceWarnings);
        Log(message);
    }
    // Preserve the normal fence handoff and status checks even on a VRAM warning.
    m_presentedFrames = state.numFramesActuallyPresented == ~0u ? 0 : state.numFramesActuallyPresented;
    if (!missingFence)
    {
        *inputFence = static_cast<ID3D12Fence*>(state.inputsProcessingCompletionFence);
        (*inputFence)->AddRef();
        inputFenceValue = state.lastPresentInputsProcessingCompletionFenceValue;
    }
    if (m_enabled && state.status != sl::DLSSGStatus::eOk)
    {
        char message[128]{};
        snprintf(message, sizeof(message), "[DLSS FG] Runtime status failure: 0x%X; requesting safe disable.", unsigned(state.status));
        Log(message);
        return Check(sl::Result::eErrorInvalidState, "DLSS-G runtime status");
    }
    return true;
}

bool StreamlineIntegration::DisableAfterFailure()
{
    // The bridge drained the presenting queue, including the FG work ordered by
    // eBlockPresentingClientQueue. Never treat missing fence data as permission
    // to immediately reuse inputs, nor reuse a previous frame's completion value.
    m_failed = true;
    m_optionsSet = false; // Force Off even if our cached mode was already Off.
    if (!SetEnabled(false) || !ClearTags())
    {
        Log("! [DLSS FG] Cannot disable frame generation safely; recovery failed.");
        return false;
    }
    return true;
}

void StreamlineIntegration::Shutdown()
{
    if (m_initialized)
    {
        if (m_deviceReady) { SetEnabled(false); ClearTags(); }
        Check(m_slShutdown(), "slShutdown");
        if (s_support.load() == Support::Supported) s_support.store(Support::RestartRequired);
    }
    m_initialized = m_deviceReady = m_enabled = false;
    m_optionsSet = false;
    m_failed = false;
    m_vramWarningLogged = false;
    m_diagnosticPresents = 0;
    m_frame = nullptr;
    m_frameLimitUs = ~0u;
}

void StreamlineIntegration::Unload()
{
    if (m_module) { FreeLibrary(m_module); m_module = nullptr; }
}
