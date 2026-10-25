#include "host_window.h"
#include "host_input_sdl.h"

#if defined(_WIN32) || defined(TTT2_HOST_GAME_UI)
#include "kernel/sys_ui.h"
#endif

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <new>
#ifdef __ANDROID__
#include <chrono>
#include <condition_variable>
#include <mutex>
#endif

namespace host_window {

#ifdef __ANDROID__
namespace {
std::mutex lifecycleMutex;
std::condition_variable lifecycleChanged;
bool foreground = true;
bool quitting = false;
uint64_t surfaceGeneration = 1;

bool SDLCALL LifecycleEvent(void*, SDL_Event* event)
{
    std::lock_guard lock(lifecycleMutex);
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        foreground = false;
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        // Even at unchanged dimensions, Android has replaced the native
        // window. A swapchain created for the previous surface is invalid.
        ++surfaceGeneration;
        foreground = true;
        break;
    case SDL_EVENT_QUIT:
    case SDL_EVENT_TERMINATING:
        quitting = true;
        break;
    default:
        return true;
    }
    lifecycleChanged.notify_all();
    return true;
}
} // namespace

bool WatchLifecycle() { return SDL_AddEventWatch(LifecycleEvent, nullptr); }
void UnwatchLifecycle() { SDL_RemoveEventWatch(LifecycleEvent, nullptr); }

bool WaitForDrawable(SDL_Window* window, uint64_t& generation)
{
    std::unique_lock lock(lifecycleMutex);
    for (;;) {
        lifecycleChanged.wait(lock, [] { return foreground || quitting; });
        if (quitting) return false;
        if (SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
            generation = surfaceGeneration;
            return true;
        }
        // Surface destruction can precede SDL's background notification.
        // Do not enter DXVK while there is no native window to present to.
        lifecycleChanged.wait_for(lock, std::chrono::milliseconds(10));
    }
}
#endif

struct Window {
    SDL_Window* handle = nullptr;
    bool textInput = false;
};

Window* Open(uint32_t width, uint32_t height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[window] SDL video init: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    // Android creates an EGL surface for the default OpenGL window. DXVK needs
    // the native window reserved for Vulkan when SDL_CreateWindow runs.
    // SDL fullscreen enables Android's immersive sticky mode, hiding both
    // system bars and restoring them only temporarily for system gestures.
    flags |= SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
#endif
    SDL_Window* handle = SDL_CreateWindow("Tekken Tag Tournament 2 (recomp)",
                                          int(width), int(height), flags);
    if (!handle) {
        std::fprintf(stderr, "[window] SDL window: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    Window* window = new (std::nothrow) Window{handle};
    if (!window) {
        SDL_DestroyWindow(handle);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    host_input::Start();
    host_input::SetFocused((SDL_GetWindowFlags(handle) & SDL_WINDOW_INPUT_FOCUS) != 0);
    return window;
}

void Close(Window* window)
{
    if (!window) return;
    if (window->textInput) SDL_StopTextInput(window->handle);
    host_input::Stop();
    SDL_DestroyWindow(window->handle);
    delete window;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

Events Poll(Window* window)
{
    Events result;
    if (!window) return result;
    #if defined(_WIN32) || defined(TTT2_HOST_GAME_UI)
    const bool wantsText = SysUiTextEntryActive();
    if (wantsText != window->textInput) {
        if (wantsText) {
            window->textInput = SDL_StartTextInput(window->handle);
            if (!window->textInput)
                std::fprintf(stderr, "[input] SDL_StartTextInput: %s\n", SDL_GetError());
        } else {
            SDL_StopTextInput(window->handle);
            window->textInput = false;
        }
    }
    #endif
    host_input::Tick();
    SDL_Event event;
#ifdef __ANDROID__
    // android_game_entry.cpp pumps SDL on its video thread. Draining the queue
    // here avoids SDL_PollEvent's implicit off-thread SDL_PumpEvents call.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0) {
#else
    while (SDL_PollEvent(&event)) {
#endif
        host_input::Event(event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(window->handle))) {
            result.close = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                   event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)) {
            result.toggleMode = true;
        }
        #if defined(_WIN32) || defined(TTT2_HOST_GAME_UI)
        if (event.type == SDL_EVENT_KEY_DOWN) {
            const int key = host_input::LegacyKey(event.key.key);
            if (!event.key.repeat && SysUiKeyCaptureActive() && key) SysUiOnKey(key);
            if (SysUiTextEntryActive()) {
                if (key == 0x08 || (!event.key.repeat && key == 0x1B))
                    SysUiOnChar(uint32_t(key));
                else if (!event.key.repeat && key == 0x0D) SysUiOnChar('\r');
            }
        } else if (event.type == SDL_EVENT_TEXT_INPUT && SysUiTextEntryActive()) {
            // Profile names are ASCII; the UI ignores other characters.
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(event.text.text);
                 *p; ++p)
                if (*p < 0x80) SysUiOnChar(*p);
        }
        #endif
    }
    return result;
}

void ApplyMode(Window* window, Mode mode, uint32_t width, uint32_t height)
{
    if (!window) return;
    SDL_Window* handle = window->handle;
#ifdef __ANDROID__
    // Desktop display modes must not turn off SDL's Android immersive mode.
    if (!SDL_SetWindowFullscreen(handle, true))
        std::fprintf(stderr, "[window] Android fullscreen: %s\n", SDL_GetError());
    return;
#endif
    const SDL_DisplayID display = SDL_GetDisplayForWindow(handle);
    SDL_Rect bounds{0, 0, int(width), int(height)};
    if (display) {
        if (mode == Mode::Windowed)
            SDL_GetDisplayUsableBounds(display, &bounds);
        else
            SDL_GetDisplayBounds(display, &bounds);
    }
    // DXGI owns exclusive fullscreen on Windows. SDL owns the window's border
    // and placement, including borderless mode on every host platform.
    SDL_SetWindowFullscreen(handle, false);
    if (mode == Mode::Windowed) {
        SDL_SetWindowBordered(handle, true);
        const int maxWidth = std::max(320, bounds.w - 80);
        const int maxHeight = std::max(180, bounds.h - 80);
        const double scale = std::min({1.0, double(maxWidth) / width,
                                      double(maxHeight) / height});
        const int clientWidth = std::max(1, int(width * scale));
        const int clientHeight = std::max(1, int(height * scale));
        SDL_SetWindowSize(handle, clientWidth, clientHeight);
        SDL_SetWindowPosition(handle,
                              bounds.x + (bounds.w - clientWidth) / 2,
                              bounds.y + (bounds.h - clientHeight) / 2);
    } else {
        SDL_SetWindowBordered(handle, false);
        SDL_SetWindowPosition(handle, bounds.x, bounds.y);
        SDL_SetWindowSize(handle, bounds.w, bounds.h);
    }
}

bool Blit(Window* window, const uint32_t* bgrx, uint32_t width, uint32_t height)
{
    if (!window || !bgrx) return false;
    SDL_Surface* target = SDL_GetWindowSurface(window->handle);
    if (!target) return false;
    SDL_Surface* source = SDL_CreateSurfaceFrom(int(width), int(height),
                                                SDL_PIXELFORMAT_XRGB8888,
                                                const_cast<uint32_t*>(bgrx),
                                                int(width * sizeof(uint32_t)));
    if (!source) return false;
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    const bool copied = SDL_BlitSurfaceScaled(source, nullptr, target, nullptr,
                                              SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(source);
    return copied && SDL_UpdateWindowSurface(window->handle);
}

void Idle(unsigned milliseconds) { SDL_Delay(milliseconds); }

SDL_Window* SDLHandle(Window* window) { return window ? window->handle : nullptr; }

NativeSurface GetNativeSurface(Window* window)
{
    SDL_Window* handle = SDLHandle(window);
    if (!handle) return {};
#ifdef _WIN32
    return {reinterpret_cast<uintptr_t>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(handle), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))};
#else
    return {reinterpret_cast<uintptr_t>(handle)};
#endif
}

} // namespace host_window
