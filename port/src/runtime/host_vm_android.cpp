#include "host_vm.h"
#include "guest_memory.h"

#include <algorithm>

#if !defined(__ANDROID__) || !defined(__aarch64__)
#error "The Android guest VM backend requires Android ARM64"
#endif

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <memory>
#include <new>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

namespace host_vm {
namespace {
constexpr size_t PageSize = 4096; // The guest GPU and write-watch call sites use 4 KiB pages.
constexpr uint8_t Armed = 1, Dirty = 2;
constexpr uint8_t NoAccess = 0, Read = 1, Write = 2;

uint8_t* g_base = nullptr;
size_t g_bytes = 0;
std::unique_ptr<uint8_t[]> g_state;
std::unique_ptr<uint8_t[]> g_permission;
std::unique_ptr<std::atomic<uint8_t>[]> g_rollbackPages;
uint8_t* g_mirrorStage = nullptr;
uintptr_t g_mirrorStart = 0;
size_t g_mirrorBytes = 0;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct sigaction g_previous{};
bool g_handlerInstalled = false;
uintptr_t g_deferredFirst = 0, g_deferredLast = 0;
DeferredFaultHandler g_deferredHandler = nullptr;
std::atomic<UnhandledFaultHandler> g_unhandledHandler{nullptr};
CaptureRollbackWriter g_captureRollback = nullptr;
RollbackWriteHandler g_rollbackWrite = nullptr;
std::atomic<bool> g_rollbackRegistered{false};
int g_faultPipe[2] = {-1, -1};
// A guest can return from main while this worker is still waiting for faults.
// pthread_t has no joinable-thread destructor; ReleaseGuest joins explicitly.
pthread_t g_faultWorker{};
bool g_faultWorkerStarted = false;
std::atomic<pid_t> g_faultWorkerTid{0};

struct FaultRequest
{
    uintptr_t address;
    bool rollback = false;
    uint64_t writer = 0;
    alignas(4) int done = 0;
    int handled = 0;
};

struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};

bool Range(void* address, size_t bytes, size_t& first, size_t& last)
{
    if (!g_base || !bytes) { errno = EINVAL; return false; }
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (start < base || start - base >= g_bytes || bytes > g_bytes - (start - base))
    {
        errno = EINVAL;
        return false;
    }
    first = (start - base) / PageSize;
    last = (start - base + bytes + PageSize - 1) / PageSize;
    return true;
}

bool ProtectRun(size_t first, size_t last, int native)
{
    return mprotect(g_base + first * PageSize, (last - first) * PageSize, native) == 0;
}

// Caller holds g_lock. Protect before clearing dirty bits so a subsequent write
// cannot pass through a writable PTE without being observed by the fault handler.
bool ResetLocked(size_t first, size_t last)
{
    for (size_t page = first; page < last;)
    {
        if (g_permission[page] != Write) { g_state[page++] = Armed; continue; }
        const size_t begin = page;
        while (page < last && g_permission[page] == Write) ++page;
        if (!ProtectRun(begin, page, PROT_READ)) return false;
        for (size_t i = begin; i < page; ++i) g_state[i] = Armed;
    }
    return true;
}

void* FaultWorker(void*)
{
    g_faultWorkerTid.store(static_cast<pid_t>(syscall(SYS_gettid)),
                           std::memory_order_release);
    for (;;)
    {
        FaultRequest* request = nullptr;
        ssize_t got;
        do { got = read(g_faultPipe[0], &request, sizeof(request)); }
        while (got < 0 && errno == EINTR);
        if (got != sizeof(request) || !request) break;
        request->handled = request->rollback
            ? g_rollbackRegistered.load(std::memory_order_acquire) && g_rollbackWrite(request->address, request->writer)
            : g_deferredHandler && g_deferredHandler(request->address);
        __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
        syscall(SYS_futex, &request->done, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }
    g_faultWorkerTid.store(0, std::memory_order_release);
    return nullptr;
}

bool DeferFault(uintptr_t address, bool rollback = false)
{
    // The worker must not wait for itself. A recursive fault means that the
    // callback touched the protected range and needs a different implementation.
    if (static_cast<pid_t>(syscall(SYS_gettid)) ==
        g_faultWorkerTid.load(std::memory_order_acquire))
        return false;
    FaultRequest request{address};
    request.rollback = rollback;
    if (rollback) request.writer = g_captureRollback(address);
    FaultRequest* pointer = &request;
    ssize_t sent;
    do { sent = syscall(SYS_write, g_faultPipe[1], &pointer, sizeof(pointer)); }
    while (sent < 0 && errno == EINTR);
    if (sent != sizeof(pointer)) return false;
    while (__atomic_load_n(&request.done, __ATOMIC_ACQUIRE) == 0)
        syscall(SYS_futex, &request.done, FUTEX_WAIT_PRIVATE, 0,
                nullptr, nullptr, 0);
    return request.handled != 0;
}

void Chain(int number, siginfo_t* info, void* context)
{
    const auto& previous = g_previous;
    if (previous.sa_handler == SIG_IGN) return;
    if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN)
    {
        if (previous.sa_flags & SA_SIGINFO)
            previous.sa_sigaction(number, info, context);
        else
            previous.sa_handler(number);
        return;
    }
    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    if (sigaction(number, &defaultAction, nullptr) != 0)
        _exit(128 + number);
    // The current signal is blocked until this handler returns. Queue it for
    // this thread, then let the kernel apply the restored default disposition.
    if (syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), number) < 0)
        _exit(128 + number);
}

void Fault(int number, siginfo_t* info, void* context)
{
    // This ARM64 kernel reports both PROT_NONE and write-watch PROT_READ
    // faults on the shared mirror as SEGV_MAPERR. Use the guest permission and
    // watch state to decide whether a write may be opened, and let the mirror
    // worker handle still-closed cached pages.
    if (number == SIGSEGV && info &&
        (info->si_code == SEGV_ACCERR || info->si_code == SEGV_MAPERR) &&
        context && g_base)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
        const uintptr_t address = reinterpret_cast<uintptr_t>(info->si_addr);
        // An armed writable page is mapped PROT_READ, so only a write can
        // fault here. The permission table avoids decoding the ARM64 ESR.
        if (address >= base && address - base < g_bytes)
        {
            const size_t page = (address - base) / PageSize;
            if (g_rollbackPages[page].load(std::memory_order_acquire) &&
                g_rollbackRegistered.load(std::memory_order_acquire) && DeferFault(address, true)) return;
            while (g_lock.test_and_set(std::memory_order_acquire)) {}
            // A second thread may have opened the page while this thread entered
            // the handler. Retrying its instruction is then sufficient.
            if (g_permission[page] == Write && g_state[page] == Dirty)
            {
                g_lock.clear(std::memory_order_release);
                return;
            }
            if (g_permission[page] == Write && g_state[page] == Armed &&
                syscall(SYS_mprotect, g_base + page * PageSize, PageSize,
                        PROT_READ | PROT_WRITE) == 0)
            {
                g_state[page] = Dirty;
                g_lock.clear(std::memory_order_release);
                return;
            }
            g_lock.clear(std::memory_order_release);
        }
        if (address >= g_deferredFirst && address < g_deferredLast &&
            g_deferredHandler)
        {
            if (DeferFault(address)) return;
        }
    }
    if (auto handler = g_unhandledHandler.load(std::memory_order_acquire))
        handler(number, info, context);
    Chain(number, info, context);
}
} // namespace

// ARM64 generated stores and host writes must use guest_memory's null guard.
// An unexpected direct null store is reported by host_fault instead of
// attempting x86-style instruction single stepping.
void CancelNullPageStep(void*) {}

uint8_t* ReserveGuest(size_t bytes)
{
    if (g_base || bytes == 0 || bytes % PageSize || sysconf(_SC_PAGESIZE) != PageSize)
    {
        errno = EINVAL;
        return nullptr;
    }
    void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (memory == MAP_FAILED) return nullptr;
    const size_t pages = bytes / PageSize;
    auto state = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[pages]);
    auto permission = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[pages]);
    auto rollbackPages = std::unique_ptr<std::atomic<uint8_t>[]>(new (std::nothrow) std::atomic<uint8_t>[pages]);
    if (!state || !permission || !rollbackPages) { munmap(memory, bytes); errno = ENOMEM; return nullptr; }
    for (size_t page = 0; page < pages; ++page)
    {
        state[page] = Armed;
        permission[page] = Write;
        rollbackPages[page].store(0, std::memory_order_relaxed);
    }
    g_base = static_cast<uint8_t*>(memory);
    g_bytes = bytes;
    g_state = std::move(state);
    g_permission = std::move(permission);
    g_rollbackPages = std::move(rollbackPages);
    struct sigaction action{};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = Fault;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &action, &g_previous) != 0)
    {
        const int failure = errno;
        ReleaseGuest(g_base, bytes);
        errno = failure;
        return nullptr;
    }
    g_handlerInstalled = true;
    if (mprotect(g_base, bytes, PROT_READ) != 0)
    {
        const int failure = errno;
        ReleaseGuest(g_base, bytes);
        errno = failure;
        return nullptr;
    }
    return g_base;
}

void ReleaseGuest(uint8_t* base, size_t bytes)
{
    if (base != g_base || bytes != g_bytes || !base) return;
    if (g_faultWorkerStarted)
    {
        close(g_faultPipe[1]);
        g_faultPipe[1] = -1;
        pthread_join(g_faultWorker, nullptr);
        g_faultWorkerStarted = false;
        close(g_faultPipe[0]);
        g_faultPipe[0] = -1;
    }
    g_deferredFirst = g_deferredLast = 0;
    g_deferredHandler = nullptr;
    g_rollbackRegistered.store(false, std::memory_order_release);
    g_captureRollback = nullptr;
    g_rollbackWrite = nullptr;
    if (g_handlerInstalled) sigaction(SIGSEGV, &g_previous, nullptr);
    g_handlerInstalled = false;
    if (g_mirrorStage) munmap(g_mirrorStage, g_mirrorBytes);
    g_mirrorStage = nullptr;
    g_mirrorStart = 0;
    g_mirrorBytes = 0;
    munmap(base, bytes);
    g_state.reset();
    g_permission.reset();
    g_rollbackPages.reset();
    g_base = nullptr;
    g_bytes = 0;
}

bool Protect(void* address, size_t bytes, Protection protection)
{
    size_t first = 0, last = 0;
    if (!Range(address, bytes, first, last)) return false;
    const uint8_t permission = protection == Protection::None ? NoAccess :
        protection == Protection::ReadOnly ? Read : Write;
    Lock lock;
    const int native = permission == NoAccess ? PROT_NONE :
        permission == Read ? PROT_READ : PROT_READ | PROT_WRITE;
    if (!ProtectRun(first, last, native)) return false;
    for (size_t page = first; page < last; ++page)
    {
        g_permission[page] = permission;
        // Explicitly opening a page RW bypasses the watch fault. Mark it dirty
        // conservatively until the caller rearms that range with ResetWriteWatch.
        if (permission == Write) g_state[page] = Dirty;
    }
    return true;
}

bool EnableNullPageGuard()
{
    if (!g_base || g_bytes < guest_memory::NullGuardBytes ||
        !Protect(g_base, guest_memory::NullGuardBytes, Protection::ReadOnly)) return false;
    return true;
}

bool QueryReadRegion(const void* address, size_t maxBytes,
                     size_t& regionBytes, bool& readable)
{
    if (!g_base || !maxBytes) { errno = EINVAL; return false; }
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (start < base || start - base >= g_bytes) { errno = EINVAL; return false; }
    const size_t offset = start - base;
    const size_t available = (std::min)(maxBytes, g_bytes - offset);
    const size_t first = offset / PageSize;
    const size_t last = (offset + available - 1) / PageSize + 1;
    Lock lock;
    readable = g_permission[first] != NoAccess;
    size_t end = first + 1;
    while (end < last && (g_permission[end] != NoAccess) == readable) ++end;
    const uintptr_t limit = base + end * PageSize;
    regionBytes = (std::min)(available, static_cast<size_t>(limit - start));
    return true;
}

bool ReadMemory(const void* address, void* output, size_t bytes)
{
    if (!bytes) return true;
    size_t first = 0, last = 0;
    if (!Range(const_cast<void*>(address), bytes, first, last)) return false;
    size_t done = 0;
    while (done < bytes)
    {
        iovec local{static_cast<uint8_t*>(output) + done, bytes - done};
        iovec remote{static_cast<uint8_t*>(const_cast<void*>(address)) + done,
                     bytes - done};
        const ssize_t got = syscall(SYS_process_vm_readv, getpid(),
                                    &local, 1ul, &remote, 1ul, 0ul);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        done += size_t(got);
    }
    return true;
}

bool CanWrite(const void* address, size_t bytes)
{
    if (!bytes) return true;
    size_t first = 0, last = 0;
    if (!Range(const_cast<void*>(address), bytes, first, last)) return false;
    Lock lock;
    for (size_t page = first; page < last; ++page)
        if (g_permission[page] != Write) return false;
    return true;
}

bool PrepareHostWrite(void* address, size_t bytes)
{
    if (!bytes) return true;
    size_t first = 0, last = 0;
    if (!Range(address, bytes, first, last)) return false;
    if (g_rollbackRegistered.load(std::memory_order_acquire))
        for (size_t page = first; page < last; ++page)
        {
            if (!g_rollbackPages[page].load(std::memory_order_acquire)) continue;
            const uintptr_t a = uintptr_t(g_base + page * PageSize);
            g_rollbackWrite(a, g_captureRollback(a));
        }
    Lock lock;
    for (size_t page = first; page < last; ++page)
        if (g_permission[page] != Write) { errno = EACCES; return false; }
    if (!ProtectRun(first, last, PROT_READ | PROT_WRITE)) return false;
    for (size_t page = first; page < last; ++page) g_state[page] = Dirty;
    return true;
}

bool ResetWriteWatch(void* address, size_t bytes)
{
    size_t first = 0, last = 0;
    if (!Range(address, bytes, first, last)) return false;
    Lock lock;
    return ResetLocked(first, last);
}

bool TakeWriteWatch(void* address, size_t bytes, void** pages,
                    size_t capacity, size_t& count, bool reset)
{
    size_t first = 0, last = 0;
    if (!Range(address, bytes, first, last) || (!pages && capacity)) return false;
    Lock lock;
    // A clean, writable guest page is already host-read-only. No rearm needed.
    if (!std::memchr(g_state.get() + first, Dirty, last - first)) {
        count = 0;
        return true;
    }
    size_t written = 0;
    for (size_t page = first; page < last; ++page)
        if (g_state[page] == Dirty) ++written;
    if (written > capacity) { errno = ENOBUFS; return false; }
    count = written;
    for (size_t page = first, out = 0; page < last && out < written; ++page)
        if (g_state[page] == Dirty)
            pages[out++] = g_base + page * PageSize;
    // With no dirty pages, writable guest pages are already armed (PROT_READ).
    // Keep the lock across this decision so a fault cannot open one unnoticed.
    // Avoid repeating mprotect for every use of an unchanged texture.
    if (reset && written && !ResetLocked(first, last)) return false;
    return true;
}

bool InstallMirrorBacking(void* address, size_t bytes)
{
    size_t first = 0, last = 0;
    if (g_mirrorStage || !Range(address, bytes, first, last) ||
        reinterpret_cast<uintptr_t>(address) % PageSize || bytes % PageSize)
    {
        errno = EINVAL;
        return false;
    }
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "ttt2-physical-mirror", MFD_CLOEXEC));
    if (fd < 0) return false;
    if (ftruncate(fd, static_cast<off_t>(bytes)) != 0)
    {
        const int failure = errno;
        close(fd);
        errno = failure;
        return false;
    }
    void* stage = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (stage == MAP_FAILED)
    {
        const int failure = errno;
        close(fd);
        errno = failure;
        return false;
    }
    // This address belongs to ReserveGuest. Only this cached-view subrange is
    // replaced; the second mapping remains writable for the mirror worker.
    void* guest = mmap(address, bytes, PROT_NONE, MAP_SHARED | MAP_FIXED, fd, 0);
    const int failure = errno;
    close(fd);
    if (guest == MAP_FAILED)
    {
        munmap(stage, bytes);
        errno = failure;
        return false;
    }
    {
        Lock lock;
        for (size_t page = first; page < last; ++page)
        {
            g_permission[page] = NoAccess;
            g_state[page] = Armed;
        }
    }
    g_mirrorStage = static_cast<uint8_t*>(stage);
    g_mirrorStart = reinterpret_cast<uintptr_t>(address);
    g_mirrorBytes = bytes;
    return true;
}

bool FillMirrorView(void* address, const void* source, size_t bytes)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (!g_mirrorStage || start < g_mirrorStart || bytes == 0 ||
        (start - g_mirrorStart) % PageSize || bytes % PageSize ||
        start - g_mirrorStart >= g_mirrorBytes ||
        bytes > g_mirrorBytes - (start - g_mirrorStart))
    {
        errno = EINVAL;
        return false;
    }
    const size_t offset = start - g_mirrorStart;
    const size_t first = (start - reinterpret_cast<uintptr_t>(g_base)) / PageSize;
    const size_t last = first + bytes / PageSize;
    {
        Lock lock;
        for (size_t page = first; page < last; ++page)
            if (g_permission[page] != NoAccess) { errno = EBUSY; return false; }
    }
    // The guest view remains PROT_NONE for the entire copy. Publish fully
    // populated pages together as readable and write-watch armed.
    std::memcpy(g_mirrorStage + offset, source, bytes);
    Lock lock;
    for (size_t page = first; page < last; ++page)
        if (g_permission[page] != NoAccess) { errno = EBUSY; return false; }
    if (!ProtectRun(first, last, PROT_READ)) return false;
    for (size_t page = first; page < last; ++page)
    {
        g_permission[page] = Write;
        g_state[page] = Armed;
    }
    return true;
}

unsigned long LastError() { return static_cast<unsigned long>(errno); }

bool RegisterUnhandledFaultHandler(UnhandledFaultHandler handler)
{
    UnhandledFaultHandler expected = nullptr;
    if (!handler || !g_unhandledHandler.compare_exchange_strong(
            expected, handler, std::memory_order_release,
            std::memory_order_relaxed))
    {
        errno = EINVAL;
        return false;
    }
    return true;
}

namespace {
bool EnsureFaultWorker()
{
    if (g_faultWorkerStarted) return true;
    if (pipe2(g_faultPipe, O_CLOEXEC) != 0) return false;
    const int startError = pthread_create(&g_faultWorker, nullptr, FaultWorker, nullptr);
    if (startError)
    {
        close(g_faultPipe[0]);
        close(g_faultPipe[1]);
        g_faultPipe[0] = g_faultPipe[1] = -1;
        errno = startError;
        return false;
    }
    g_faultWorkerStarted = true;
    return true;
}
}

bool ProtectForRollback(void* address, size_t bytes, bool readOnly)
{
    size_t first = 0, last = 0;
    if (!Range(address, bytes, first, last)) return false;
    Lock lock;
    // Tracked image ranges include immutable code, and heaps can contain
    // inaccessible pages. Rollback must preserve those logical permissions.
    for (size_t page = first; page < last;)
    {
        if (g_permission[page] != Write) { ++page; continue; }
        const size_t begin = page;
        while (page < last && g_permission[page] == Write) ++page;
        // Publish the mask before removing write access: a simultaneous writer
        // must never bypass the observer through the write-watch fallback.
        if (readOnly)
            for (size_t i = begin; i < page; ++i) g_rollbackPages[i].store(1, std::memory_order_release);
        if (!ProtectRun(begin, page, readOnly ? PROT_READ : PROT_READ | PROT_WRITE)) return false;
        if (!readOnly)
            for (size_t i = begin; i < page; ++i) g_rollbackPages[i].store(0, std::memory_order_release);
        if (!readOnly)
            for (size_t i = begin; i < page; ++i) g_state[i] = Dirty;
    }
    return true;
}

bool RegisterRollbackWriteHandler(CaptureRollbackWriter capture, RollbackWriteHandler handler)
{
    if (!capture || !handler || g_rollbackWrite) { errno = EINVAL; return false; }
    if (!EnsureFaultWorker()) return false;
    g_captureRollback = capture;
    g_rollbackWrite = handler;
    g_rollbackRegistered.store(true, std::memory_order_release);
    return true;
}

bool RegisterDeferredFaultHandler(void* address, size_t bytes,
                                  DeferredFaultHandler handler)
{
    size_t first = 0, last = 0;
    if (!handler || !Range(address, bytes, first, last) || g_deferredHandler)
    {
        errno = EINVAL;
        return false;
    }
    if (!EnsureFaultWorker()) return false;
    g_deferredFirst = reinterpret_cast<uintptr_t>(g_base) + first * PageSize;
    g_deferredLast = reinterpret_cast<uintptr_t>(g_base) + last * PageSize;
    g_deferredHandler = handler;
    return true;
}

} // namespace host_vm
