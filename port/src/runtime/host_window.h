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

} // namespace host_window
