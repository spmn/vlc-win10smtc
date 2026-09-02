#include "host_window.h"

#include <system_error>

#include "plugin.h"

namespace
{

constexpr wchar_t kClassName[] = L"" PLUGIN_SHORTNAME;
constexpr wchar_t kTitle[] = L"" PLUGIN_DESCRIPTION;

HMODULE GetPluginModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GetPluginModule), &module);
    return module;
}

[[noreturn]] void ThrowLastError(const char* what)
{
    throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), what };
}

} // namespace

HostWindow::HostWindow()
    : module_{ GetPluginModule() }
{
    WNDCLASSEXW cls{ .cbSize = sizeof cls, .lpfnWndProc = DefWindowProcW, .hInstance = module_, .lpszClassName = kClassName };
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        ThrowLastError("RegisterClassEx");

    window_ = CreateWindowExW(0, kClassName, kTitle, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, module_, nullptr);
    if (!window_)
        ThrowLastError("CreateWindowEx");
}

HostWindow::~HostWindow()
{
    DestroyWindow(window_);
    UnregisterClassW(kClassName, module_);   // fails harmlessly while another instance still has a window
}
