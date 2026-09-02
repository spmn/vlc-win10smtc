#pragma once

#include <windows.h>

// Hidden top-level window for APIs that need one. Create and destroy it on the same thread, and
// keep that thread pumping messages.
class HostWindow
{
public:
    HostWindow();
    ~HostWindow();

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    HWND GetHandle() const { return window_; }

private:
    HMODULE module_;
    HWND window_ = nullptr;
};
