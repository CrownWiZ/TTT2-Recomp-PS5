// PS5 runtime host (interface: host_runtime.h) — fixed homebrew layout.
//
// The Relapse payload loader runs the ELF with content writable under
// /data/homebrew. The layout is fixed: /data/homebrew/ttt2 carries the
// generated game files (tekken.img + game_files_x, uploaded with the FTP
// server that M3 left resident) and the process-local stores (saves, config,
// cache, logs). There is no /proc and no fork/exec here: shader prebuild is
// in-process and optional; the null-page guard stays off by platform.
#include "host_runtime.h"
#include "host_vm.h"
#include "host_thread.h"
#include "kernel/sce_ps5.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

#ifndef TTT2_HOST_PS5
#error "This runtime host backend requires the PS5 build"
#endif

namespace host_runtime {
namespace {
std::filesystem::path DataRoot()
{
    static const std::filesystem::path root = []
    {
        const char* overridden = std::getenv("TTT2_DEBUG_ROOT");
        return std::filesystem::path(overridden && *overridden ? overridden : "/data/homebrew/ttt2");
    }();
    return root;
}
}

std::filesystem::path InstalledLayout(int argc)
{
    // Console defaults: hardware renderer on, frame dumps off, display-
    // plane surface constantly exclusive.
    setenv("TTT2_GPU_HW", "1", 0);
    setenv("TTT2_FBDUMP", "0", 0);
    setenv("TTT2_VFS_ROOT", (DataRoot() / "game_files_x").c_str(), 0);
    std::error_code error;
    std::filesystem::create_directories(DataRoot() / "saves", error);
    std::filesystem::create_directories(DataRoot() / "config", error);
    std::filesystem::create_directories(DataRoot() / "cache", error);
    std::filesystem::create_directories(DataRoot() / "logs", error);
    setenv("DXVK_LOG_PATH", (DataRoot() / "cache").c_str(), 1);
    setenv("DXVK_HUD", "0", 1);
    if (argc > 1) return {};
    const auto image = DataRoot() / "tekken.img";
    return std::filesystem::is_regular_file(image, error) && !error ? image : std::filesystem::path{};
}

std::filesystem::path VfsRoot()
{
    const char* configured = std::getenv("TTT2_VFS_ROOT");
    return configured && *configured ? std::filesystem::path(configured)
                                     : DataRoot() / "game_files_x";
}

const char* DefaultImagePath() { return "/data/homebrew/ttt2/tekken.img"; }
const char* DefaultVfsRoot() { return "/data/homebrew/ttt2/game_files_x"; }

void StartLogWriter()
{
    const auto root = DataRoot();
    std::error_code error;
    std::filesystem::create_directories(root / "logs", error);
    if (error) return;
    const int log = open((root / "logs" / "last.log").c_str(),
                         O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (log < 0) return;
    dup2(log, 2 /*stderr*/);
    close(log);
}

void ConfigureTimer() {}

void InstallFatalHandler() {}

void InstallGuestGuards()
{
    // The null-page guard is Linux-only code; on the PS5 it stays disabled by
    // platform (spec 93-95) and the recomp validates without it.
}

void host_runtime::StartShaderPrebuild() {}

} // namespace host_runtime

// kernel_shared.h's contract is the global one, as on Linux: stderr is
// unbuffered on the console, so there is nothing to drain.
void LogDrain(unsigned) {}

// In-process notification toast announcing the boot, mirroring the M3 cycle.
extern "C" void Ps5Notify(const char* message)
{
    notify_request_t request{};
    std::snprintf(request.message, sizeof(request.message), "%s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}
