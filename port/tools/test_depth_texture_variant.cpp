// CPU-only eligibility and shader-cache compatibility checks. Optional arguments
// are captured pixel microcode files; no game assets are part of this test.
#include "../src/runtime/kernel/gpu_render.cpp"
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

static unsigned checks = 0;
static void Check(bool value) {
    ++checks;
    if (!value) { std::fprintf(stderr, "Depth texture variant check %u failed\n", checks); std::exit(1); }
}
static void CheckTranslation(const ttt2_hw::PsTranslation& t) {
    const auto generic = ttt2_hw::PixelShaderHlsl(t);
    Check(generic == ttt2_hw::PixelShaderHlsl(t, false));
    const auto specialized = ttt2_hw::PixelShaderHlsl(t, true);
    const auto resolved = ttt2_hw::PixelShaderHlsl(t, ttt2_hw::TextureStateVariant::ResolvedColor);
    if (!ttt2_hw::HasTextureStateVariants(t)) {
        Check(generic == specialized); Check(generic == resolved); return;
    }
    Check(generic != specialized); Check(generic != resolved);
    const auto entry = generic.find("struct PSIn");
    Check(entry != std::string::npos);
    // Depth export and alpha testing retain the identical entry point.
    Check(generic.substr(entry) == specialized.substr(specialized.find("struct PSIn")));
    Check(generic.substr(entry) == resolved.substr(resolved.find("struct PSIn")));
    for (unsigned slot = 0; slot < 32; ++slot) {
        if (!(t.textureSlots & (1u << slot))) continue;
        const auto index = "[" + std::to_string(slot) + "]";
        Check(specialized.find("texSampler" + index) == std::string::npos);
        Check(specialized.find("texScale" + index) == std::string::npos);
        Check(specialized.find("texInfo" + index + ".x") == std::string::npos);
        Check(specialized.find("texInfo" + index + ".y") == std::string::npos);
        Check(resolved.find("texSampler" + index) == std::string::npos);
        Check(resolved.find("texInfo" + index + ".x") == std::string::npos);
        Check(resolved.find("texInfo" + index + ".y") == std::string::npos);
        // Resolved surfaces can occupy only part of an allocated texture.
        // Their scale stays dynamic, unlike the unit-scale depth variant.
        Check((generic.find("texScale" + index) != std::string::npos) ==
              (resolved.find("texScale" + index) != std::string::npos));
    }
}
int main(int argc, char** argv) {
    std::array<uint32_t, 4> sampler{2, 2, 0, 0};
    auto eligible = [&](uint32_t swizzle, uint32_t mode, float x, float y) {
        return ttt2_hw::StaticDepthTextureState(swizzle, mode, x, y, sampler.data());
    };
    Check(eligible(0x688, 3, 1, 1));
    for (uint32_t bit = 1; bit; bit <<= 1) Check(!eligible(0x688 ^ bit, 3, 1, 1));
    for (uint32_t mode = 0; mode < 16; ++mode) Check(eligible(0x688, mode, 1, 1) == (mode == 3));
    for (float scale : {0.f, .5f, 2.f, std::nextafter(1.f, 0.f), std::nextafter(1.f, 2.f),
                       std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        Check(!eligible(0x688, 3, scale, 1)); Check(!eligible(0x688, 3, 1, scale));
    }
    for (unsigned field = 0; field < 4; ++field) {
        const auto old = sampler[field];
        for (unsigned value = 0; value < 8; ++value) {
            sampler[field] = value;
            Check(eligible(0x688, 3, 1, 1) == (value == old));
        }
        sampler[field] = old;
    }
    const std::array<uint32_t, 4> resolvedSampler{2, 2, 1, 0};
    sampler = resolvedSampler;
    auto resolvedEligible = [&](uint32_t format, uint32_t swizzle, uint32_t mode) {
        return ttt2_hw::StaticResolvedColorTextureState(format, swizzle, mode, sampler.data());
    };
    Check(resolvedEligible(FMT_8_8_8_8, 0x60A, 2));
    for (unsigned format = 0; format < 64; ++format)
        Check(resolvedEligible(format, 0x60A, 2) == (format == FMT_8_8_8_8));
    for (unsigned swizzle = 0; swizzle < 4096; ++swizzle)
        Check(resolvedEligible(FMT_8_8_8_8, swizzle, 2) == (swizzle == 0x60A));
    for (unsigned mode = 0; mode < 16; ++mode)
        Check(resolvedEligible(FMT_8_8_8_8, 0x60A, mode) == (mode == 2));
    for (unsigned field = 0; field < 4; ++field) {
        for (unsigned value = 0; value < 8; ++value) {
            sampler[field] = value;
            Check(resolvedEligible(FMT_8_8_8_8, 0x60A, 2) == (value == resolvedSampler[field]));
        }
        sampler[field] = resolvedSampler[field];
    }
    for (unsigned slot = 0; slot < 32; ++slot) {
        ttt2_hw::PsTranslation t;
        t.textureSlots = 1u << slot;
        const auto index = "[" + std::to_string(slot) + "]";
        t.function = "texSampler" + index + " texScale" + index + " texInfo" + index + ".x texInfo" + index + ".y";
        CheckTranslation(t);
        t.writesDepth = true; CheckTranslation(t);
        t.cubeSlots = t.textureSlots; CheckTranslation(t);
        t.cubeSlots = t.textureSlots = 0; CheckTranslation(t);
    }
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary | std::ios::ate);
        Check(bool(f)); const auto bytes = f.tellg(); Check(bytes > 0 && bytes % 4 == 0);
        std::vector<uint32_t> code(size_t(bytes) / 4); f.seekg(0);
        f.read(reinterpret_cast<char*>(code.data()), bytes); Check(bool(f));
        const auto t = ttt2_hw::TranslatePixelShader(code);
        Check(t.unsupported.empty()); CheckTranslation(t);
    }
    std::printf("PASS %u depth-texture variant checks\n", checks);
}
