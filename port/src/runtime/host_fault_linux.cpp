#include "host_fault.h"
#include "host_vm.h"

#if !defined(__linux__) || !defined(__x86_64__)
#error "The Linux guest fault backend currently requires x86-64 Linux"
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <csetjmp>
#include <iterator>
#include <memory>
#include <new>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>

namespace host_fault {
namespace {
struct Frame
{
    sigjmp_buf jump;
    Frame* previous;
};

thread_local Frame* t_frame = nullptr;
struct FaultDetails
{
    volatile sig_atomic_t signal;
    volatile uintptr_t address;
    volatile uintptr_t pc;
    volatile sig_atomic_t write;
};
thread_local FaultDetails t_fault{};
constexpr int OtherFaults[] = {SIGBUS, SIGILL, SIGFPE};
struct sigaction g_previous[3]{};
constexpr size_t AlternateStackBytes = 64 * 1024;
// Linux UAPI SS_AUTODISARM. The fault boundary uses siglongjmp rather than
// returning through sigreturn, so the stack must be disarmed on handler entry.
constexpr int AutoDisarm = static_cast<int>(1u << 31);

struct AlternateStack
{
    stack_t previous{};
    bool installed = false;
    int error = 0;

    AlternateStack()
    {
        static thread_local std::unique_ptr<char[]> storage;
        if (!storage) storage.reset(new (std::nothrow) char[AlternateStackBytes]);
        if (!storage) { error = ENOMEM; return; }
        stack_t current{};
        current.ss_sp = storage.get();
        current.ss_size = AlternateStackBytes;
        current.ss_flags = AutoDisarm;
        installed = sigaltstack(&current, &previous) == 0;
        if (!installed) error = errno;
    }

    ~AlternateStack()
    {
        // The buffer is thread-local and may be freed at thread exit. Never
        // leave the kernel pointing at it after this guarded call ends.
        if (installed && sigaltstack(&previous, nullptr) != 0) std::abort();
    }
};

void Unhandled(int number, void* rawInfo, void* rawContext)
{
    Frame* frame = t_frame;
    if (!frame) return;
    auto* info = static_cast<siginfo_t*>(rawInfo);
    auto* context = static_cast<ucontext_t*>(rawContext);
    t_fault.signal = number;
    t_fault.address = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
    t_fault.pc = context ? uintptr_t(context->uc_mcontext.gregs[REG_RIP]) : 0;
    t_fault.write = (number == SIGSEGV || number == SIGBUS) && context &&
                    (context->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    siglongjmp(frame->jump, 1);
}

void OtherFault(int number, siginfo_t* info, void* context)
{
    host_vm::CancelNullPageStep(context);
    if (t_frame) Unhandled(number, info, context);
    for (size_t i = 0; i < std::size(OtherFaults); ++i)
    {
        if (OtherFaults[i] != number) continue;
        const auto& previous = g_previous[i];
        if (previous.sa_handler == SIG_IGN) return;
        if (previous.sa_handler != SIG_DFL)
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
        if (syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), number) < 0)
            _exit(128 + number);
        return; // The default disposition runs when this handler returns.
    }
}

bool RegisterOtherFaults()
{
    struct sigaction action{};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = OtherFault;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (size_t i = 0; i < std::size(OtherFaults); ++i)
        if (sigaction(OtherFaults[i], &action, &g_previous[i]) != 0)
        {
            while (i) { --i; sigaction(OtherFaults[i], &g_previous[i], nullptr); }
            return false;
        }
    return true;
}
}

bool Run(Procedure procedure, void* argument)
{
    // The guest VM installs the SIGSEGV handler before the first guest call.
    // Function-local initialization serializes the first calls from guest threads.
    static const int registrationError = [] {
        if (!host_vm::RegisterUnhandledFaultHandler(Unhandled) ||
            !RegisterOtherFaults()) return errno ? errno : EINVAL;
        return 0;
    }();
    if (registrationError)
    {
        static const bool reported = [] {
            std::fprintf(stderr, "[fault] cannot install guest fault handlers (%d)\n",
                         registrationError);
            return true;
        }();
        (void)reported;
        return false;
    }
    AlternateStack alternateStack;
    if (!alternateStack.installed)
    {
        static thread_local bool warned = false;
        if (!warned)
        {
            std::fprintf(stderr, "[fault] alternate signal stack unavailable (%d)\n",
                         alternateStack.error);
            warned = true;
        }
        return false;
    }
    Frame frame{};
    frame.previous = t_frame;
    if (sigsetjmp(frame.jump, 1) == 0)
    {
        t_frame = &frame;
        try { procedure(argument); }
        catch (...)
        {
            t_frame = frame.previous;
            throw;
        }
        t_frame = frame.previous;
        return true;
    }
    t_frame = frame.previous;
    const int signal = t_fault.signal;
    if (signal == SIGSEGV || signal == SIGBUS)
        std::fprintf(stderr, "[fault] signal %d host=0x%llX pc=0x%llX %s\n",
                     signal, (unsigned long long)t_fault.address,
                     (unsigned long long)t_fault.pc, t_fault.write ? "write" : "read");
    else
        std::fprintf(stderr, "[fault] signal %d pc=0x%llX\n",
                     signal, (unsigned long long)t_fault.pc);
    return false;
}
}
