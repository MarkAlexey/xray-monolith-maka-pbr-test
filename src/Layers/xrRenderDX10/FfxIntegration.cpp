#include "FfxIntegration.h"
#include <softpub.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
std::atomic<FfxApi::Logger> s_logger{};
std::atomic<unsigned> s_messages{};

// Generic embedded-signature check (any trusted publisher). Streamline's helper is
// NVIDIA-specific; wintrust is loaded on demand so no import is added.
bool VerifySignature(const wchar_t* path)
{
    using PfnWinVerifyTrust = LONG(WINAPI*)(HWND, GUID*, LPVOID);
    HMODULE wintrust = LoadLibraryExW(L"wintrust.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wintrust) return false;
    const auto verify = reinterpret_cast<PfnWinVerifyTrust>(GetProcAddress(wintrust, "WinVerifyTrust"));
    bool trusted = false;
    if (verify)
    {
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = path;
        GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        WINTRUST_DATA data{};
        data.cbStruct = sizeof(data);
        data.dwUIChoice = WTD_UI_NONE;
        data.fdwRevocationChecks = WTD_REVOKE_NONE;
        data.dwUnionChoice = WTD_CHOICE_FILE;
        data.pFile = &file;
        data.dwStateAction = WTD_STATEACTION_VERIFY;
        trusted = verify(nullptr, &policy, &data) == ERROR_SUCCESS;
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        verify(nullptr, &policy, &data);
    }
    FreeLibrary(wintrust);
    return trusted;
}
}

void FfxApi::Message(uint32_t type, const wchar_t* message)
{
    auto logger = s_logger.load();
    if (!logger || !message) return;
    char text[512]{};
    char converted[448]{};
    WideCharToMultiByte(CP_UTF8, 0, message, -1, converted, int(sizeof(converted) - 1), nullptr, nullptr);
    // Debug checking can repeat a warning every frame: drop consecutive repeats,
    // and cap distinct messages. Messages arrive on the render thread.
    static char previous[448]{};
    if (!strcmp(previous, converted)) return;
    strcpy_s(previous, converted);
    const unsigned count = ++s_messages;
    if (count > 64) return;
    snprintf(text, sizeof(text), "%s[FSR] %s%s", type == FFX_API_MESSAGE_TYPE_ERROR ? "! " : "", converted,
        count == 64 ? " (further FSR runtime messages suppressed)" : "");
    logger(text);
}

bool FfxApi::Load(Logger logger, bool debugMessages)
{
    if (m_module) return true;
    s_logger.store(logger);
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    if (!length || length >= std::size(path)) { m_failureStage = "Executable path"; return false; }
    std::wstring loader(path);
    loader.resize(loader.find_last_of(L"\\/") + 1);
    loader += L"amd_fidelityfx_loader_dx12.dll";
    if (GetFileAttributesW(loader.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        m_failureStage = "amd_fidelityfx_loader_dx12.dll missing beside the executable";
        return false;
    }
    if (!VerifySignature(loader.c_str()))
    {
        m_failureStage = "AMD FSR loader signature verification";
        return false;
    }
    // Default search order: the loader then finds the upscaler/frame-generation
    // DLLs in the application directory.
    m_module = LoadLibraryW(loader.c_str());
    if (!m_module) { m_failureStage = "Load AMD FSR loader"; return false; }
    m_create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(m_module, "ffxCreateContext"));
    m_destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(m_module, "ffxDestroyContext"));
    m_configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(m_module, "ffxConfigure"));
    m_query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(m_module, "ffxQuery"));
    m_dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(m_module, "ffxDispatch"));
    if (!m_create || !m_destroy || !m_configure || !m_query || !m_dispatch)
    {
        m_failureStage = "AMD FSR loader exports";
        Unload();
        return false;
    }
    if (debugMessages)
    {
        ffxConfigureDescGlobalDebug1 debug{};
        debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        debug.fpMessage = &FfxApi::Message;
        debug.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        m_configure(nullptr, &debug.header);
    }
    return true;
}

void FfxApi::Unload()
{
    if (m_module) FreeLibrary(m_module);
    m_module = nullptr;
    m_create = nullptr;
    m_destroy = nullptr;
    m_configure = nullptr;
    m_query = nullptr;
    m_dispatch = nullptr;
}

std::string FfxApi::ProviderVersion(ffxContext* context) const
{
    if (!m_module || !context || !*context) return "unknown";
    ffxQueryGetProviderVersion query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    // Version names may live in global memory that later queries overwrite: copy.
    if (m_query(context, &query.header) != FFX_API_RETURN_OK || !query.versionName) return "unknown";
    return query.versionName;
}
