// PS5 entry point. elfldr hands control to _start with the process parameters
// block and the loader's teardown callback; there is no C runtime of ours in
// between, so this file is what stands in for crt1.o:
//
//   IEEE floating point -> platform runtime -> atexit teardown -> .preinit/
//   .init arrays -> main -> exit.
//
// The FP reset matters: a title starts with flush-to-zero and
// denormals-are-zero set, and both the recompiled PPC code and DXVK's maths
// assume IEEE behaviour (the platform layer's fp.h).
#include <cstddef>
#include <cstdint>

using Destructor = void (*)();
using Initializer = void (*)();

extern "C" {
void _init_env(void* processParameters);
void ps5_fp_ieee(void);
int atexit(Destructor callback);
[[noreturn]] void exit(int status);
int application_main(int argc, char** argv, char** envp) __asm__("main");

extern Initializer __preinit_array_start[] __attribute__((weak));
extern Initializer __preinit_array_end[] __attribute__((weak));
extern Initializer __init_array_start[] __attribute__((weak));
extern Initializer __init_array_end[] __attribute__((weak));
extern Initializer __fini_array_start[] __attribute__((weak));
extern Initializer __fini_array_end[] __attribute__((weak));
}

namespace {

void RunForward(Initializer* first, Initializer* last)
{
    if (!first || !last) return;
    while (first != last) (*first++)();
}

void RunReverse(Initializer* first, Initializer* last)
{
    if (!first || !last) return;
    while (last != first) (*--last)();
}

} // namespace

extern "C" __attribute__((weak)) void catchReturnFromMain(int status) { (void)status; }

extern "C" void _init()
{
    RunForward(__preinit_array_start, __preinit_array_end);
    RunForward(__init_array_start, __init_array_end);
}

extern "C" void _fini()
{
    RunReverse(__fini_array_start, __fini_array_end);
}

extern "C" [[noreturn]] __attribute__((visibility("default")))
void _start(void* processParameters, Destructor loaderTeardown)
{
    const int argc = *static_cast<const int*>(processParameters);
    auto* bytes = static_cast<std::uint8_t*>(processParameters);
    auto** argv = reinterpret_cast<char**>(bytes + sizeof(std::uint64_t));

    ps5_fp_ieee();
    _init_env(processParameters);
    if (loaderTeardown) (void)atexit(loaderTeardown);
    (void)atexit(_fini);
    _init();

    const int status = application_main(argc, argv, nullptr);
    catchReturnFromMain(status);
    exit(status);
}
