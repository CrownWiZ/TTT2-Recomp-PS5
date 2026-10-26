#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "host_window_sdl.h"

#include <atomic>
#include <cstdio>
#include <thread>

int ttt2_game_main(int argc, char** argv);

int main(int argc, char** argv)
{
    // Android SDL requires event pumping on the thread that initialized video.
    // The guest entry point does not return until the game exits, so it runs on
    // a worker while this thread pumps Android lifecycle and input events.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[android] SDL video init: %s\n", SDL_GetError());
        return 1;
    }
    if (!host_window::WatchLifecycle()) {
        std::fprintf(stderr, "[android] SDL lifecycle watch: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 1;
    }

    std::atomic<bool> finished{false};
    int result = 1;
    std::thread game([&] {
        result = ttt2_game_main(argc, argv);
        finished.store(true, std::memory_order_release);
    });
    while (!finished.load(std::memory_order_acquire)) {
        SDL_PumpEvents();
        SDL_Delay(5);
    }
    game.join();
    host_window::UnwatchLifecycle();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}
