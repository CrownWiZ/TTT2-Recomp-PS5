#pragma once

// SDL types stay opaque so window-less platforms compile this contract:
// only host_window_sdl.cpp includes the real SDL headers.
struct SDL_Window;
#include <cstdint>

namespace host_window {

enum class Mode { Windowed, Borderless, Fullscreen };

struct Events {
    bool close = false;
    bool toggleMode = false;
};

struct Window;

Window* Open(uint32_t width, uint32_t height);
void Close(Window* window);
Events Poll(Window* window);
void ApplyMode(Window* window, Mode mode, uint32_t width, uint32_t height);
bool Blit(Window* window, const uint32_t* bgrx, uint32_t width, uint32_t height);
void Idle(unsigned milliseconds);
SDL_Window* SDLHandle(Window* window);

#ifdef __ANDROID__
// SDL's event pump and our renderer run on different threads. The renderer
// must honor Android's lifecycle independently of SDL_PumpEvents blocking.
bool WatchLifecycle();
void UnwatchLifecycle();
bool WaitForDrawable(SDL_Window* window, uint64_t& generation);
#endif

} // namespace host_window
