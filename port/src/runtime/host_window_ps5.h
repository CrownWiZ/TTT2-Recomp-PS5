#pragma once
// PS5 video shell: declares the display token DXVK's `display` WSI interprets
// as an HWND, so the port owns the value the renderer passes on.
#include <cstdint>

namespace host_window {
inline constexpr uintptr_t kDisplayWindowToken = 1;
} // namespace host_window
