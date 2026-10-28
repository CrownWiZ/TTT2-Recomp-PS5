// PS5 video shell (interface: host_window.h / host_window_sdl.h) — sceVideoOut.
//
// There is no window manager: Open() opens the TV (the MAIN VideoOut bus),
// registers two exclusive CPU-written BGRX framebuffers in direct memory for
// the pre-DXVK fallback and splash path, and exposes the display token that
// DXVG Native's `display` WSI interprets as an HWND for the real swapchain.
// Poll always reports no events; ApplyMode is a no-op (the console owns the
// mode); Idle sleeps the caller.
#include "host_window_ps5.h"
#include "host_window.h"
#include "kernel/sce_ps5.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace host_window {

namespace {

struct Shell
{
    std::mutex mutex;
    int32_t videoOut = -1;
    int32_t userId = -1;
    uint32_t width = 1280, height = 720;
    int64_t dmem = -1;
    void* mapping = nullptr;
    size_t frameBytes = 0;
    uint32_t* frames[2] = { nullptr, nullptr };
    int nextFrame = 0;
};

Shell& VideoShell() { static auto* shell = new Shell; return *shell; }

int64_t FrameBytesFor(uint32_t width, uint32_t height)
{
    return int64_t(width) * int64_t(height) * 4;
}

} // namespace

Window* Open(uint32_t width, uint32_t height)
{
    auto& shell = VideoShell();
    std::lock_guard lock(shell.mutex);
    if (shell.videoOut >= 0)
    {
        // One TV: subsequent opens reuse the shell; the caller's size request
        // only matters for where the DXVK swapchain reports its extent.
        return reinterpret_cast<Window*>(&shell);
    }

    shell.width = width;
    shell.height = height;
    shell.frameBytes = size_t(FrameBytesFor(width, height));

    if (sceUserServiceGetInitialUser(&shell.userId) != 0) shell.userId = -1;
    shell.videoOut = sceVideoOutOpen(-1, SCE_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (shell.videoOut < 0)
    {
        std::fprintf(stderr, "[virtual-display] sceVideoOutOpen failed\n");
        return nullptr;
    }

    // Two exclusive frame buffers in direct memory, 1 MiB aligned.
    const size_t total = shell.frameBytes * 2;
    const size_t aligned = (total + size_t(SCE_KERNEL_DMEM_ALIGNMENT - 1)) & ~size_t(SCE_KERNEL_DMEM_ALIGNMENT - 1);
    if (sceKernelAllocateDirectMemory(0, int64_t(SCE_KERNEL_MAIN_DMEM_SIZE),
                                      aligned, size_t(SCE_KERNEL_DMEM_ALIGNMENT),
                                      SCE_KERNEL_WB_ONION, &shell.dmem) != 0)
    {
        std::fprintf(stderr, "[virtual-display] direct memory allocation failed\n");
        return nullptr;
    }
    void* mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, aligned, 3 /*read/write*/, 0, shell.dmem, aligned) != 0)
    {
        std::fprintf(stderr, "[virtual-display] direct memory mapping failed\n");
        return nullptr;
    }
    shell.mapping = mapped;
    std::memset(mapped, 0, total);
    shell.frames[0] = static_cast<uint32_t*>(mapped);
    shell.frames[1] = reinterpret_cast<uint32_t*>(static_cast<char*>(mapped) + shell.frameBytes);

    SceVideoOutBufferAttributeV2 attribute{};
    attribute.pixelFormat = SCE_VIDEO_OUT_PIXEL_FORMAT_B8_G8_R8_X8;
    attribute.width = width;
    attribute.height = height;
    attribute.pitchInPixel = width;
    attribute.mem = mapped;
    {
        const void* buffers[2] = { shell.frames[0], shell.frames[1] };
        if (sceVideoOutRegisterBuffers2(shell.videoOut, 0, 2, buffers, &attribute) != 0)
        {
            std::fprintf(stderr, "[virtual-display] buffer registration failed\n");
            return nullptr;
        }
    }
    std::fprintf(stderr, "[virtual-display] TV open: %u x %u, out %d\n",
                 width, height, shell.videoOut);
    return reinterpret_cast<Window*>(&shell);
}

void Close(Window* window)
{
    // One TV: shell state persists across the process; the game's own window
    // teardown all backs on VideoOut device closes. Keep the flip path alive.
    (void)window;
}

Events Poll(Window* window)
{
    (void)window;
    return { }; // No close buttons on a console.
}

void ApplyMode(Window* window, Mode mode, uint32_t width, uint32_t height)
{
    // The display-plane surface is already exclusive-by-construction.
    (void)window; (void)mode; (void)width; (void)height;
}

bool Blit(Window* window, const uint32_t* bgrx, uint32_t width, uint32_t height)
{
    auto& shell = VideoShell();
    (void)window;
    if (shell.videoOut < 0 || !shell.frames[0]) return false;
    if (width != shell.width || height != shell.height) return false;
    uint32_t* frame = shell.frames[shell.nextFrame];
    std::memcpy(frame, bgrx, shell.frameBytes);
    if (sceVideoOutSubmitFlip(shell.videoOut, shell.nextFrame, 1 /*queue*/, 0) != 0)
        return false;
    shell.nextFrame ^= 1;
    return true;
}

void Idle(unsigned milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

SDL_Window* SDLHandle(Window* window)
{
    // The `display` WSI interprets the window token (display WS tokens contract).
    return window ? reinterpret_cast<SDL_Window*>(reinterpret_cast<void*>(kDisplayWindowToken))
                  : nullptr;
}

NativeSurface GetNativeSurface(Window* window)
{
    NativeSurface surface;
    surface.window = window ? kDisplayWindowToken : 0;
    return surface;
}

void DisplaySize(uint32_t* width, uint32_t* height)
{
    auto& shell = VideoShell();
    std::lock_guard lock(shell.mutex);
    if (width) *width = shell.width;
    if (height) *height = shell.height;
}

} // namespace host_window
