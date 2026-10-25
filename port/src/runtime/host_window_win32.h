#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace host_window { struct Window; }

namespace host_window_win32 {
HWND Handle(host_window::Window* window);
void InstallMessageHook(host_window::Window* window);
}
