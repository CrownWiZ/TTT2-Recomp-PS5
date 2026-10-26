#pragma once

#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unwind.h>

void KernelDumpGuestThreads();
namespace hitch { void DumpGuestThreads(); void DumpNativeSamples(); }

void DumpAllStacks(const char* why)
{
    std::fprintf(stderr, "[stacks] %s\n", why ? why : "thread dump");
    KernelDumpGuestThreads();
    hitch::DumpGuestThreads();
    hitch::DumpNativeSamples();
    std::error_code error;
    for (const auto& task : std::filesystem::directory_iterator("/proc/self/task", error))
    {
        std::string name, state, wait;
        std::ifstream(task.path() / "comm") >> name;
        std::ifstream(task.path() / "wchan") >> wait;
        std::ifstream status(task.path() / "status");
        for (std::string line; std::getline(status, line);)
            if (line.starts_with("State:")) { state = line.substr(6); break; }
        std::fprintf(stderr, "[stacks] tid=%s name=%s state=%s wait=%s\n",
            task.path().filename().c_str(), name.c_str(), state.c_str(), wait.c_str());
    }
    std::fprintf(stderr, "[stacks] calling thread native frames:\n");
    unsigned frames = 0;
    _Unwind_Backtrace([](_Unwind_Context* context, void* argument) {
        auto& count = *static_cast<unsigned*>(argument);
        const uintptr_t address = _Unwind_GetIP(context);
        if (!address || count++ == 32) return _URC_END_OF_STACK;
        Dl_info symbol{};
        if (dladdr(reinterpret_cast<void*>(address), &symbol) && symbol.dli_fbase)
            std::fprintf(stderr, "[stacks] %s+%llX %s\n", symbol.dli_fname ? symbol.dli_fname : "?",
                (unsigned long long)(address - uintptr_t(symbol.dli_fbase)), symbol.dli_sname ? symbol.dli_sname : "");
        else std::fprintf(stderr, "[stacks] %llX\n", (unsigned long long)address);
        return _URC_NO_REASON;
    }, &frames);
}
