// CPU-only Linux/Android check of SDL semaphore semantics and audio-rate waits.
#include <SDL3/SDL.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sys/resource.h>
#include <thread>
#include <time.h>

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); std::exit(1); }
}
static double CpuSeconds() {
    timespec value{};
    Require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0, "thread CPU clock");
    return value.tv_sec + value.tv_nsec * 1e-9;
}
static void Interrupt(int) {}

int main() {
    SDL_Semaphore* sem = SDL_CreateSemaphore(0);
    Require(sem != nullptr, "create");
    Require(!SDL_TryWaitSemaphore(sem), "empty semaphore must not succeed");
    SDL_SignalSemaphore(sem);
    Require(SDL_TryWaitSemaphore(sem), "consume one posted token");
    Require(!SDL_TryWaitSemaphore(sem), "token consumed exactly once");
    auto start = std::chrono::steady_clock::now();
    Require(!SDL_WaitSemaphoreTimeout(sem, 20), "empty timed wait must expire");
    Require(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(19), "timeout returned early");

    struct sigaction action{};
    action.sa_handler = Interrupt;
    sigemptyset(&action.sa_mask);
    Require(sigaction(SIGUSR1, &action, nullptr) == 0, "install interruption handler");
    const auto waiter = pthread_self();
    std::thread signal([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pthread_kill(waiter, SIGUSR1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        SDL_SignalSemaphore(sem);
    });
    const bool interruptedWait = SDL_WaitSemaphoreTimeout(sem, 1000);
    signal.join();
    Require(interruptedWait, "interrupted timed wait must resume until signaled");
    Require(!SDL_TryWaitSemaphore(sem), "interrupted wait consumed its token");
    std::thread indefinite([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        SDL_SignalSemaphore(sem);
    });
    SDL_WaitSemaphore(sem);
    indefinite.join();
    Require(!SDL_TryWaitSemaphore(sem), "indefinite wait consumed its token");

    constexpr int count = 400;
    std::thread producer([&] {
        auto deadline = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            deadline += std::chrono::microseconds(5333);
            std::this_thread::sleep_until(deadline);
            SDL_SignalSemaphore(sem);
        }
    });
    rusage before{}, after{};
    Require(getrusage(RUSAGE_THREAD, &before) == 0, "initial resource counters");
    const double cpu = CpuSeconds();
    start = std::chrono::steady_clock::now();
    int received = 0;
    for (int i = 0; i < count; ++i) received += SDL_WaitSemaphoreTimeout(sem, 1000);
    const double cpuUsed = CpuSeconds() - cpu;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    Require(getrusage(RUSAGE_THREAD, &after) == 0, "final resource counters");
    producer.join();
    Require(received == count && !SDL_TryWaitSemaphore(sem), "all timed tokens consumed exactly once");
    std::printf("PASS tokens=%d seconds=%.3f cpu_ms_s=%.3f voluntary_switches=%ld involuntary_switches=%ld\n",
        received, elapsed, cpuUsed * 1000 / elapsed,
        after.ru_nvcsw - before.ru_nvcsw, after.ru_nivcsw - before.ru_nivcsw);
    SDL_DestroySemaphore(sem);
}
