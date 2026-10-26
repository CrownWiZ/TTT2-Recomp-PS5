#include "kernel/phys_mirror.h"

#if !defined(_WIN32)
#error "The Windows physical-mirror fault hook requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
uintptr_t g_first = 0;
uintptr_t g_last = 0;
PVOID g_handler = nullptr;

LONG CALLBACK MirrorVeh(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* record = ep->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t fault = record->ExceptionInformation[1];
    if (fault < g_first || fault >= g_last) return EXCEPTION_CONTINUE_SEARCH;

    const auto result = PhysMirrorHandleFault(fault);
    if (result == PhysMirrorFaultResult::Unhandled) return EXCEPTION_CONTINUE_SEARCH;

    static const bool log = [] { char value[2]{};
                                 return GetEnvironmentVariableA("TTT2_MIRROR_LOG", value, sizeof(value)) == 1 &&
                                        value[0] == '1'; }();
    static std::atomic<uint32_t> seen{0};
    const uint32_t n = seen.fetch_add(1, std::memory_order_relaxed);
    if (log && result == PhysMirrorFaultResult::Filled && (n < 400 || (n & 127) == 0))
    {
        const uint64_t module = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
        uint64_t callers[3] = {};
        CONTEXT walk = *ep->ContextRecord;
        for (uint64_t& caller : callers)
        {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(walk.Rip, &imageBase, nullptr);
            if (!function)
            {
                walk.Rip = *reinterpret_cast<const DWORD64*>(walk.Rsp);
                walk.Rsp += 8;
            }
            else
            {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, walk.Rip, function,
                                 &walk, &handlerData, &establisher, nullptr);
            }
            if (!walk.Rip) break;
            caller = walk.Rip - module;
        }
        std::fprintf(stderr,
                     "[phys-mirror] fill %08X %s pc RVA=0x%llX callers 0x%llX 0x%llX 0x%llX\n",
                     uint32_t(fault - g_first + 0xC0000000u),
                     record->ExceptionInformation[0] ? "write" : "read",
                     (unsigned long long)(reinterpret_cast<uint64_t>(record->ExceptionAddress) - module),
                     (unsigned long long)callers[0], (unsigned long long)callers[1],
                     (unsigned long long)callers[2]);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}
} // namespace

bool PhysMirrorInstallFaultHandler(uint8_t* view, size_t bytes)
{
    if (!view || !bytes || g_handler) return false;
    g_first = reinterpret_cast<uintptr_t>(view);
    g_last = g_first + bytes;
    g_handler = AddVectoredExceptionHandler(1, MirrorVeh);
    return g_handler != nullptr;
}
