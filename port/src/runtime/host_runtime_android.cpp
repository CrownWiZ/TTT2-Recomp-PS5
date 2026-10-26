#include "host_runtime.h"
#include "host_vm.h"
#include "host_runtime_posix_diagnostics.h"

#if !defined(__ANDROID__) || !defined(__aarch64__)
#error "Android runtime host requires ARM64 Android"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace host_runtime {
namespace {
std::filesystem::path g_dataRoot;

std::filesystem::path DataRoot()
{
    if (!g_dataRoot.empty()) return g_dataRoot;
    const char* external = SDL_GetAndroidExternalStoragePath();
    if (external && *external) g_dataRoot = external;
    else {
        char* fallback = SDL_GetPrefPath("TTT2", "Recomp");
        if (fallback) { g_dataRoot = fallback; SDL_free(fallback); }
    }
    return g_dataRoot;
}

std::filesystem::path GameRoot()
{
    const auto root = DataRoot();
    std::error_code error;
    if (std::filesystem::is_regular_file(root / "installed-game" / "installed.txt", error))
        return root / "installed-game";
    return root;
}

void SetDefault(const char* key, const std::filesystem::path& value)
{
    if (!value.empty()) setenv(key, value.c_str(), 0);
}

int ReadWorkerLimit(const std::filesystem::path& path, int fallback)
{
    int value = 0;
    if (std::ifstream input(path); input && (input >> value) && value >= 1 && value <= 8)
        return value;
    return fallback;
}
} // namespace

std::filesystem::path InstalledLayout(int argc)
{
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    setenv("TTT2_AUDIO_PACE", "1", 0);
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);
    // Keep direct launches without app storage hidden as well.
    setenv("DXVK_HUD", "0", 1);
    setenv("TTT2_SHADER_WORKERS", "2", 0);
    setenv("TTT2_GPU_HW", "1", 0);
    setenv("TTT2_FBDUMP", "0", 0);
    // The Android DXVK raw constant-buffer patch also fixes the GPU vertex path.
    setenv("TTT2_GPU_VS", "1", 0);
    setenv("TTT2_VS_FETCH_SPECIALIZE", "1", 0);
    setenv("TTT2_WORKER_BARRIER_WAIT_US", "10", 0);
    const auto root = DataRoot();
    if (root.empty()) return {};
    std::error_code error;
    std::filesystem::create_directories(root / "saves", error);
    std::filesystem::create_directories(root / "config", error);
    std::filesystem::create_directories(root / "cache", error);
    std::filesystem::create_directories(root / "logs", error);
    // Create the HUD items once; the render thread polls this file for live
    // visibility changes, including when resuming an existing game session.
    setenv("DXVK_TTT2_HUD_FILE", (root / "config" / "debug_hud.txt").c_str(), 1);
    setenv("DXVK_HUD", "fps,compiler,scale=1.3", 1);
    const int gameWorkers = ReadWorkerLimit(root / "config" / "game_shader_workers.txt", 2);
    const int dxvkWorkers = ReadWorkerLimit(root / "config" / "dxvk_shader_workers.txt", 2);
    setenv("TTT2_SHADER_WORKERS", std::to_string(gameWorkers).c_str(), 1);
    setenv("TTT2_DXVK_SHADER_WORKERS", std::to_string(dxvkWorkers).c_str(), 1);
    const auto dxvkConfig = root / "config" / "dxvk-runtime.conf";
    {
        std::ofstream output(dxvkConfig, std::ios::trunc);
        if (output) output << "dxvk.numCompilerThreads = " << dxvkWorkers << '\n';
    }
    SetDefault("DXVK_CONFIG_FILE", dxvkConfig);
    SetDefault("DXVK_TTT2_READBACK_MODE_FILE", root / "config" / "readback_mode.txt");
    if (std::filesystem::exists(root / "config" / "xma_log"))
        setenv("TTT2_XMA_LOG", "1", 1);
    if (std::filesystem::exists(root / "config" / "xma_trace_stereo"))
        setenv("TTT2_XMA_TRACE_STEREO", "1", 1);
    if (std::filesystem::exists(root / "config" / "gpu_stage_timing"))
        setenv("TTT2_GPU_STAGE_TIMING", "1", 1);
    if (std::filesystem::exists(root / "config" / "pacing_probe"))
    {
        setenv("TTT2_PACING_PROBE", "1", 1);
        SetDefault("TTT2_PACING_TRACE_FILE", root / "logs" / "pacing_trace.request");
    }
    if (std::filesystem::exists(root / "config" / "gpu_frametime"))
        setenv("TTT2_FRAMETIME", "1", 1);
    if (std::filesystem::exists(root / "config" / "skin_profile"))
        setenv("TTT2_SKIN_PROFILE", "1", 1);
    if (std::filesystem::exists(root / "config" / "cpu_vertex_profile"))
        setenv("TTT2_CPU_VERTEX_PROFILE", "1", 1);
    SetDefault("TTT2_SKIN_NATIVE_FILE", root / "config" / "skin_native.txt");
    SetDefault("TTT2_SKIN_32_NATIVE_FILE", root / "config" / "skin32_native.txt");
    const auto packedFieldMode = root / "config" / "packed_field_mode.txt";
    if (std::filesystem::exists(packedFieldMode))
        SetDefault("TTT2_PACKED_FIELD_MODE_FILE", packedFieldMode);
    const auto drawSkipMode = root / "config" / "draw_skip.txt";
    if (std::filesystem::exists(drawSkipMode))
        SetDefault("TTT2_DRAW_SKIP_FILE", drawSkipMode);
    SetDefault("TTT2_DEBUG_ROOT", root / "logs");
    if (std::filesystem::exists(root / "config" / "audio_dump")) {
        SetDefault("TTT2_AUDIO_CAPTURE_DIR", root / "logs");
    }
    if (std::filesystem::exists(root / "config" / "frame_benchmark")) {
        std::filesystem::create_directories(root / "logs" / "benchmark", error);
        SetDefault("TTT2_BENCHMARK_DIR", root / "logs" / "benchmark");

        SetDefault("TTT2_VIDEO_HZ_FILE", root / "config" / "video_hz.txt");
    }
    SetDefault("TTT2_GEOMETRY_MODE_FILE", root / "config" / "geometry_mode.txt");
    SetDefault("TTT2_AUDIO_PACE_FILE", root / "config" / "audio_pace.txt");
    SetDefault("TTT2_SHADER_CACHE", root / "cache" / "shadercache");
    SetDefault("TTT2_VFS_ROOT", GameRoot() / "game_files_x");
    SetDefault("TTT2_SAVE_ROOT", root / "saves");
    SetDefault("XDG_CONFIG_HOME", root / "config");
    SetDefault("DXVK_STATE_CACHE_PATH", root / "cache");
    SetDefault("DXVK_PIPELINE_CACHE_FILE", root / "cache" / "dxvk-1.9.2b-ttt2-v1.vkcache");
    SetDefault("DXVK_LOG_PATH", root / "cache");
    // RESOLUTION in Display Settings owns the render scale. Older setup versions
    // created config/render_scale.txt with 0.75; leave it unused so upgrades also
    // follow the saved in-game resolution. Explicit diagnostic environment
    // overrides are handled by the renderer.
    if (argc > 1) return {};
    const auto image = GameRoot() / "tekken.img";
    return std::filesystem::is_regular_file(image, error) && !error ? image :
           std::filesystem::path{};
}

std::filesystem::path VfsRoot()
{
    const char* configured = std::getenv("TTT2_VFS_ROOT");
    return configured && *configured ? std::filesystem::path(configured) :
                                      GameRoot() / "game_files_x";
}

const char* DefaultImagePath()
{
    static const std::string path = (GameRoot() / "tekken.img").string();
    return path.c_str();
}

const char* DefaultVfsRoot()
{
    static const std::string path = (GameRoot() / "game_files_x").string();
    return path.c_str();
}

void StartLogWriter()
{
    const auto root = DataRoot();
    if (root.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(root / "logs", error);
    if (error) return;
    const int log = open((root / "logs" / "last.log").c_str(),
                         O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (log < 0) return;
    dup2(log, STDERR_FILENO);
    close(log);
    // The game creates its D3D11 device before opening the SDL window. DXVK's
    // SDL3 WSI asks SDL for Vulkan instance extensions during device creation.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[android] SDL video init: %s\n", SDL_GetError());
        std::abort();
    }
}

void ConfigureTimer() {}
void InstallFatalHandler()
{
    // Fault recovery is owned by the VM. Keep the optional scheduled diagnostic
    // dump available on native hosts through the same startup setting.
    const char* configured = std::getenv("TTT2_STACKDUMP");
    const int seconds = configured ? std::atoi(configured) : 0;
    if (seconds > 0)
        std::thread([seconds] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            DumpAllStacks("scheduled thread dump");
            std::fflush(stderr);
            _exit(3);
        }).detach();
}
void InstallGuestGuards()
{
    if (!host_vm::EnableNullPageGuard()) std::abort();
}
} // namespace host_runtime

void LogDrain(unsigned) {} // stderr is unbuffered by the common entry point.

void host_runtime::StartShaderPrebuild()
{
    // Android setup runs the shared prebuild through its explicit setup action.
}
