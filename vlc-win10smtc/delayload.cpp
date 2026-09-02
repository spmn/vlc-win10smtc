#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include <windows.h>
#include <delayimp.h>

#include "plugin.h"

namespace {

std::string ReadModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH];
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH))
        return "unknown";

    const DWORD size = GetFileVersionInfoSizeW(path, nullptr);
    if (!size)
        return "unknown";

    std::vector<std::byte> block(size);
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixed_size = 0;
    if (!GetFileVersionInfoW(path, 0, size, block.data())
        || !VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size))
        return "unknown";

    return std::format("{}.{}.{}", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                       HIWORD(fixed->dwFileVersionLS));
}

FARPROC WINAPI OnDelayLoadFailure(unsigned notify, PDelayLoadInfo info) noexcept
{
    if (notify != dliFailLoadLib && notify != dliFailGetProc)
        return nullptr;

    const std::string missing = info->dlp.fImportByName
                                    ? info->dlp.szProcName
                                    : std::format("ordinal {}", info->dlp.dwOrdinal);

    // hmodCur is null when the library itself failed to load.
    const std::string version = ReadModuleVersion(info->hmodCur);

    const std::string message = std::format(
        "{} {} ({}) could not resolve {} in {} {} (error {}).\n\n"
        "This plugin does not support this VLC build. Update or remove the plugin.\n"
        "VLC will now close.",
        PLUGIN_SHORTNAME, PLUGIN_VERSION_STR, PLUGIN_BUILD_ID, missing, info->szDll,
        version, info->dwLastError);

    MessageBoxA(nullptr, message.c_str(), PLUGIN_FILE_DESCRIPTION,
                MB_ICONERROR | MB_OK | MB_SETFOREGROUND);

    TerminateProcess(GetCurrentProcess(), VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND));
    return nullptr;
}

} // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = OnDelayLoadFailure;
