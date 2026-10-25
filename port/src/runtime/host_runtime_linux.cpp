#include "host_runtime.h"
#include "host_vm.h"
#include "host_thread.h"
#include "host_runtime_posix_diagnostics.h"
#include <cerrno>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if !defined(__linux__)
#error "This runtime host backend requires Linux"
#endif

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace host_runtime {
std::filesystem::path InstalledLayout(int argc)
{
    // DXVK Native needs the SDL3 window-system driver even when the image path
    // is passed explicitly by a development launcher.
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);
    setenv("TTT2_GPU_HW", "1", 0);
    setenv("TTT2_FBDUMP", "0", 0);
    if (argc > 1) return {};
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return {};
    const auto game = executable.parent_path() / "game";
    const auto image = game / "tekken.img";
    if (!std::filesystem::is_regular_file(image, error) || error) return {};
    const char* vfsRoot = std::getenv("TTT2_VFS_ROOT");
    if (!vfsRoot || !*vfsRoot) setenv("TTT2_VFS_ROOT", game.c_str(), 1);
    return image;
}

std::filesystem::path VfsRoot()
{
    const char* configured = std::getenv("TTT2_VFS_ROOT");
    return configured && *configured ? std::filesystem::path(configured) :
                                      std::filesystem::path(DefaultVfsRoot());
}

const char* DefaultImagePath() { return "re/decrypted/Default_patched_tu3.bin"; }
const char* DefaultVfsRoot() { return "re/game_files_x"; }
void StartLogWriter() {}
void ConfigureTimer() {} // Linux sleeps already have high-resolution timers.
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
}

void LogDrain(unsigned) {} // Linux stderr stays unbuffered in the common entry point.

void host_runtime::StartShaderPrebuild()
{
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return;
    const unsigned cores = host_thread::AvailableProcessors();
    const std::string workers = std::to_string(cores > 3 ? cores / 2 : 1);
    const std::string program = executable.string();
    const char* arguments[] = { program.c_str(), "--build-shaders", "", "", workers.c_str(), nullptr };
    const pid_t child = fork();
    if (child == 0)
    {
        // Keep the child path to raw syscalls and exec: other host threads may
        // have held libc locks when fork captured their address space.
        syscall(SYS_setpriority, PRIO_PROCESS, 0, 10);
        execv(program.c_str(), const_cast<char* const*>(arguments));
        _exit(127);
    }
    if (child < 0) return;
    std::thread([child] {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            std::fprintf(stderr, "[shaders] background prebuild exited with status %d\n", WEXITSTATUS(status));
    }).detach();
}
