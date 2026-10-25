#include "host_window_win32.h"
#include "host_window_sdl.h"

#include <SDL3/SDL.h>

namespace host_window_win32 {

HWND Handle(host_window::Window* window)
{
    SDL_Window* handle = host_window::SDLHandle(window);
    return handle ? static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(handle), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr)) : nullptr;
}

static bool SDLCALL MessageHook(void* userdata, MSG* message)
{
    if (message->hwnd != userdata) return true;
    const UINT type = message->message;
    const WPARAM key = message->wParam;
    if (type == WM_SYSCHAR ||
        (type == WM_SYSCOMMAND && (key & 0xFFF0) == SC_KEYMENU))
        return false;
    return true;
}

void InstallMessageHook(host_window::Window* window)
{
    SDL_SetWindowsMessageHook(MessageHook, Handle(window));
}

} // namespace host_window_win32
