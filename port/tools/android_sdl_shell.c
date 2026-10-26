// Android renderer-route smoke test. No game runtime or game assets are used.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("TTT2 Vulkan route probe", 960, 540,
                                           SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "vulkan");
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer(vulkan): %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }
    SDL_Log("Android SDL renderer: %s", SDL_GetRendererName(renderer));

    bool running = true;
    unsigned frames = 0;
    Uint64 last_log = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                SDL_Log("Android SDL window resized: %dx%d",
                        event.window.data1, event.window.data2);
        }
        Uint8 red = (Uint8)((frames / 2) % 256);
        SDL_SetRenderDrawColor(renderer, red, 28, 160, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        ++frames;
        const Uint64 now = SDL_GetTicks();
        if (now - last_log >= 1000) {
            SDL_Log("Android Vulkan frame count: %u", frames);
            last_log = now;
        }
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
