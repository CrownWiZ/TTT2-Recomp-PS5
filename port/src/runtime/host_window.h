#pragma once

#include "host_window_sdl.h"
#include <cstdint>

namespace host_window {

// The D3D11 ABI calls this an HWND on both platforms. DXVK Native's SDL3 WSI
// interprets the Linux value as an SDL_Window*.
struct NativeSurface {
    uintptr_t window = 0;
};

NativeSurface GetNativeSurface(Window* window);

// Size of the drawable surface in pixels. On the console there is no window to
// query, so the video shell answers instead of the OS.
void DisplaySize(uint32_t* width, uint32_t* height);

} // namespace host_window
