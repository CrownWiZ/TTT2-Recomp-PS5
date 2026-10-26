// Standalone D3D11 correctness/workload test for DXVK framebuffer reuse.
// Run with a software Vulkan ICD when the hardware GPU is unavailable.
#include <d3d11.h>
#include <d3dcompiler.h>
#if defined(_WIN32)
#include <wrl/client.h>
#else
#include "dxvk_native_com_ptr.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

static void Need(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s failed: %08x\n", operation, unsigned(result));
        std::exit(1);
    }
}

struct Target {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    unsigned width, height;
    unsigned char expected[4] = {0, 0, 0, 255};
};

static void Check(ID3D11Device* device, ID3D11DeviceContext* context, Target& target) {
    D3D11_TEXTURE2D_DESC desc;
    target.texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Need(device->CreateTexture2D(&desc, nullptr, &readback), "create readback");
    context->CopyResource(readback.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    Need(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "map readback");
    for (unsigned y = 0; y < target.height; ++y) {
        auto row = static_cast<const unsigned char*>(map.pData) + y * map.RowPitch;
        for (unsigned x = 0; x < target.width; ++x) {
            if (std::memcmp(row + x * 4, target.expected, 4)) {
                std::fprintf(stderr, "Pixel mismatch at %u,%u: %u,%u,%u,%u expected %u,%u,%u,%u\n",
                    x, y, row[x*4], row[x*4+1], row[x*4+2], row[x*4+3],
                    target.expected[0], target.expected[1], target.expected[2], target.expected[3]);
                std::exit(2);
            }
        }
    }
    context->Unmap(readback.Get(), 0);
}

int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Need(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context), "create device");
    const char* source =
        "float4 vs(uint id : SV_VertexID) : SV_Position {"
        " return float4(float2((id << 1) & 2, id & 2) * 2 - 1, 0.5, 1); }"
        "struct Out { float4 a : SV_Target0; float4 b : SV_Target1; };"
        "Out ps() { Out o; o.a=float4(1,0,0,1); o.b=float4(0,0,1,1); return o; }";
    ComPtr<ID3DBlob> vertexCode, pixelCode, errors;
    Need(D3DCompile(source, std::strlen(source), "framebuffer-test", nullptr, nullptr,
        "vs", "vs_5_0", 0, 0, &vertexCode, &errors), "compile VS");
    Need(D3DCompile(source, std::strlen(source), "framebuffer-test", nullptr, nullptr,
        "ps", "ps_5_0", 0, 0, &pixelCode, &errors), "compile PS");
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    Need(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
        nullptr, &vertex), "create VS");
    Need(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
        nullptr, &pixel), "create PS");
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterState;
    Need(device->CreateRasterizerState(&raster, &rasterState), "create rasterizer");
    context->RSSetState(rasterState.Get());
    D3D11_DEPTH_STENCIL_DESC depthStateDesc{};
    depthStateDesc.DepthEnable = TRUE;
    depthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ComPtr<ID3D11DepthStencilState> depthState;
    Need(device->CreateDepthStencilState(&depthStateDesc, &depthState), "create depth state");
    context->OMSetDepthStencilState(depthState.Get(), 0);

    // Exercise working sets below and above both experimental cache limits.
    // Destroy/recreate every phase, including all depth views, to test lifetime.
    for (unsigned phase = 0; phase < 4; ++phase) {
        unsigned count = phase == 1 ? 96 : phase == 2 ? 192 : 4;
        bool variedSizes = phase == 1 || phase == 2;
        std::vector<Target> targets(count);
        for (unsigned i = 0; i < count; ++i) {
            auto& t = targets[i];
            t.width = variedSizes ? 8 + (i % 3) * 4 : 16;
            t.height = variedSizes ? 8 + (i % 2) * 8 : 16;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = t.width; desc.Height = t.height;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            Need(device->CreateTexture2D(&desc, nullptr, &t.texture), "create color target");
            Need(device->CreateRenderTargetView(t.texture.Get(), nullptr, &t.view), "create color view");
            const float black[4] = {0, 0, 0, 1};
            context->ClearRenderTargetView(t.view.Get(), black);
        }
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = depthDesc.Height = 16;
        depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depth;
        ComPtr<ID3D11DepthStencilView> depthRW, depthRO;
        Need(device->CreateTexture2D(&depthDesc, nullptr, &depth), "create depth target");
        Need(device->CreateDepthStencilView(depth.Get(), nullptr, &depthRW), "create depth RW");
        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = depthDesc.Format;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        depthViewDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
        Need(device->CreateDepthStencilView(depth.Get(), &depthViewDesc, &depthRO), "create depth RO");
        context->ClearDepthStencilView(depthRW.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        auto start = std::chrono::steady_clock::now();
        for (unsigned draw = 0; draw < 4096; ++draw) {
            unsigned a = draw % count, b = (a + 1) % count;
            bool dual = !variedSizes && draw % 3 == 0;
            ID3D11RenderTargetView* views[] = {targets[a].view.Get(), targets[b].view.Get()};
            auto depthView = draw % 11 == 0 ? nullptr : draw % 7 == 0 ? depthRO.Get() : depthRW.Get();
            context->OMSetRenderTargets(dual ? 2 : 1, views, depthView);
            D3D11_VIEWPORT viewport{0, 0, float(targets[a].width), float(targets[a].height), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->Draw(3, 0);
            targets[a].expected[0] = 255; targets[a].expected[2] = 0;
            if (dual) { targets[b].expected[0] = 0; targets[b].expected[2] = 255; }
            if (draw % 257 == 256) context->Flush();
        }
        context->OMSetRenderTargets(0, nullptr, nullptr);
        for (auto& target : targets) Check(device.Get(), context.Get(), target);
        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start);
        std::printf("PASS phase=%u targets=%u draws=4096 elapsed_ms=%.3f\n", phase, count, elapsed.count());
    }
    context->ClearState();
    context->Flush();
}
