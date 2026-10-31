// AMD FSR SDK 2.3.0 (FFX API) signed DX12 runtime: FSR Upscaling 4.x and FSR Frame Generation.
// Loaded dynamically. The DX11 executable and DLSS-only DX12 sessions never load it.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <string>
#include "../../../sdk/FidelityFX2/api/include/dx12/ffx_api_dx12.h"
#include "../../../sdk/FidelityFX2/upscalers/include/ffx_upscale.h"
#include "../../../sdk/FidelityFX2/framegeneration/include/ffx_framegeneration.h"
#include "../../../sdk/FidelityFX2/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h"

class FfxApi final
{
public:
    using Logger = void (*)(const char*);
    FfxApi() = default;
    ~FfxApi() { Unload(); }
    FfxApi(const FfxApi&) = delete;
    FfxApi& operator=(const FfxApi&) = delete;

    // Loads amd_fidelityfx_loader_dx12.dll from the executable directory after an
    // Authenticode check. The loader finds its effect DLLs in the same directory.
    bool Load(Logger logger, bool debugMessages);
    void Unload(); // Only after every FFX context has been destroyed.
    bool Loaded() const { return m_module != nullptr; }
    const char* FailureStage() const { return m_failureStage; }

    ffxReturnCode_t CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc) const { return m_create(context, desc, nullptr); }
    ffxReturnCode_t DestroyContext(ffxContext* context) const { return m_destroy(context, nullptr); }
    ffxReturnCode_t Configure(ffxContext* context, const ffxConfigureDescHeader* desc) const { return m_configure(context, desc); }
    ffxReturnCode_t Query(ffxContext* context, ffxQueryDescHeader* desc) const { return m_query(context, desc); }
    ffxReturnCode_t Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc) const { return m_dispatch(context, desc); }

    // Provider actually selected for a created context, e.g. "4.1.1" on RDNA3/4 or a
    // 3.1.x fallback elsewhere. Returns "unknown" if the query fails.
    std::string ProviderVersion(ffxContext* context) const;
    static void Message(uint32_t type, const wchar_t* message); // Rate-limited log sink.

private:
    HMODULE m_module{};
    PfnFfxCreateContext m_create{};
    PfnFfxDestroyContext m_destroy{};
    PfnFfxConfigure m_configure{};
    PfnFfxQuery m_query{};
    PfnFfxDispatch m_dispatch{};
    const char* m_failureStage{"none"};
};
