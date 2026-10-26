// Android D3D11/DXVK presentation probe; independent of game data and PPC code.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wsi/native_wsi.h>

#include <cstdlib>

static bool check(HRESULT hr, const char* operation)
{
    if (SUCCEEDED(hr)) return true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed: 0x%08x", operation,
                 static_cast<unsigned>(hr));
    return false;
}

static bool create_target(ID3D11Device* device, IDXGISwapChain1* swap,
                          ID3D11Texture2D** back, ID3D11RenderTargetView** target)
{
    if (!check(swap->GetBuffer(0, IID_PPV_ARGS(back)), "GetBuffer")) return false;
    if (!check(device->CreateRenderTargetView(*back, nullptr, target),
               "CreateRenderTargetView")) {
        (*back)->Release();
        *back = nullptr;
        return false;
    }
    return true;
}

static void release_swapchain(ID3D11Texture2D** back, ID3D11RenderTargetView** target,
                              IDXGISwapChain1** swap)
{
    if (*target) { (*target)->Release(); *target = nullptr; }
    if (*back) { (*back)->Release(); *back = nullptr; }
    if (*swap) { (*swap)->Release(); *swap = nullptr; }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("TTT2 D3D11 Android probe", 960, 540,
                                           SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_Log("D3D11 probe SDL window ready");

    char* cache_path = SDL_GetPrefPath("TTT2", "AndroidDXVKProbe");
    if (!cache_path) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetPrefPath: %s", SDL_GetError());
        return 3;
    }
    setenv("DXVK_STATE_CACHE_PATH", cache_path, 1);
    setenv("DXVK_LOG_PATH", cache_path, 1);
    SDL_free(cache_path);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actual = D3D_FEATURE_LEVEL_9_1;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   requested, 1, D3D11_SDK_VERSION,
                                   &device, &actual, &context);
    if (!check(hr, "D3D11CreateDevice")) return 3;
    SDL_Log("D3D11 device created; feature level 0x%x", unsigned(actual));

    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain1* swap = nullptr;
    ID3D11Texture2D* back = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    int exit_code = 4;
    do {
        if (!check(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)),
                   "QueryInterface(IDXGIDevice)")) break;
        if (!check(dxgi_device->GetAdapter(&adapter), "GetAdapter")) break;
        if (!check(adapter->GetParent(IID_PPV_ARGS(&factory)),
                   "GetParent(IDXGIFactory2)")) break;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
        fullscreen.Windowed = TRUE;

        bool running = true;
        unsigned frames = 0;
        Uint64 last_log = SDL_GetTicks();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    int new_width = event.window.data1;
                    int new_height = event.window.data2;
                    if (swap && new_width > 0 && new_height > 0 &&
                        (desc.Width != unsigned(new_width) ||
                         desc.Height != unsigned(new_height)))
                        release_swapchain(&back, &target, &swap);
                }
            }
            if (!running) break;
            if (!swap) {
                void* native_window = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                    SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
                if (!native_window) { SDL_Delay(100); continue; }
                int width = 0, height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                desc.Width = width > 0 ? width : 960;
                desc.Height = height > 0 ? height : 540;
                if (!check(factory->CreateSwapChainForHwnd(device, dxvk::wsi::toHwnd(window),
                                                            &desc, &fullscreen, nullptr, &swap),
                           "CreateSwapChainForHwnd") ||
                    !create_target(device, swap, &back, &target)) {
                    release_swapchain(&back, &target, &swap);
                    SDL_Delay(100);
                    continue;
                }
                SDL_Log("D3D11 swapchain ready: %dx%d", desc.Width, desc.Height);
            }
            const float color[4] = {float((frames / 2) % 256) / 255.0f,
                                    0.08f, 0.42f, 1.0f};
            context->ClearRenderTargetView(target, color);
            if (!check(swap->Present(1, 0), "Present")) {
                release_swapchain(&back, &target, &swap);
                SDL_Delay(100);
                continue;
            }
            ++frames;
            Uint64 now = SDL_GetTicks();
            if (now - last_log >= 1000) {
                SDL_Log("D3D11/DXVK presented frames: %u", frames);
                last_log = now;
            }
        }
        exit_code = 0;
    } while (false);

    release_swapchain(&back, &target, &swap);
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    context->Release();
    device->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
