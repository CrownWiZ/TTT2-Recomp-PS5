#pragma once

#include <cstddef>
#include <cstdint>

#ifdef DrawState
#undef DrawState // winuser.h aliases this identifier to DrawStateA.
#endif

// Values passed from the Xenos command processor to a host GPU backend.
// Device resources and shader objects belong to the selected backend.
namespace ttt2_hw {

// Shared descriptor layout for translated HLSL compiled to Vulkan SPIR-V.
// VS and PS have independent b0/b1 data, so each stage uses its own set.
// HLSL b, t and s registers share one binding namespace within each set.
struct SpirvBindingLayout
{
    static constexpr uint32_t PixelSet = 0;
    static constexpr uint32_t VertexSet = 1;
    static constexpr uint32_t PsConstants = 0;
    static constexpr uint32_t PsState = 1;
    static constexpr uint32_t TextureBase = 2;
    static constexpr uint32_t TextureCount = 32;
    static constexpr uint32_t SamplerBase = TextureBase + TextureCount;
    static constexpr uint32_t SamplerCount = 16;
    static constexpr uint32_t VsConstants = 0;
    static constexpr uint32_t VsState = 1;
    static constexpr uint32_t VertexData = 2;
};

// Maximum sample-space extent of the current EDRAM-lite model on both GPU
// backends. Individual surfaces grow only to the part a draw reaches.
inline constexpr uint32_t SurfaceW = 4096;
inline constexpr uint32_t SurfaceH = 2048;

// CPU-assembled vertex for the hardware rasterizer: clip position followed by
// all sixteen Xenos interpolators.
struct HwVertex { float pos[4]; float attr[16][4]; };
static_assert(offsetof(HwVertex, attr) == 16);
static_assert(sizeof(HwVertex) == 272);

// Frontend window coordinates and interpolators before a backend converts the
// position to clip space. Rectangle synthesis uses this same representation.
// Keep the draw boundary independent of gpu_render.cpp's private Vertex type.
struct CpuVertex
{
    float x, y, z, invW;
    float attr[16][4];
};
static_assert(offsetof(CpuVertex, attr) == 16);
static_assert(sizeof(CpuVertex) == 272);

// Decoded Xenos texture fetch constant. Guest address translation happens in
// the GPU frontend; host backends only consume this description and its data.
struct TextureInfo
{
    bool valid = false;
    uint32_t base = 0, width = 1, height = 1, format = 0, pitch = 0;
    bool tiled = false;
    uint32_t swizzle = 0, endian = 0;
    uint32_t clampX = 2, clampY = 2, magFilter = 1, border = 0;
    uint32_t dimension = 1, signs = 0;
    uint32_t minFilter = 1, mipFilter = 2, aniso = 0, mipMin = 0, mipMax = 0;
    int32_t lodBias = 0;
    bool packedMips = false;
    uint32_t mipBase = 0, originX = 0, originY = 0;
};

inline constexpr uint32_t TextureFormatRgba8 = 6;
inline constexpr uint32_t TextureFormatDepth24_8 = 22;
inline constexpr uint32_t TextureFormatDepth24_8Float = 23;
inline constexpr uint32_t TextureFormatRg16Float = 31;

inline void SamplerInfo(const TextureInfo& texture, uint32_t out[4])
{
    out[0] = texture.valid ? texture.clampX : 2u;
    out[1] = texture.valid ? texture.clampY : 2u;
    out[2] = texture.valid && texture.magFilter == 1 ? 1u : 0u;
    out[3] = texture.valid ? texture.border : 0u;
}

// Byte layout of XenosPsState at HLSL register b1. Both D3D11 and Vulkan
// upload this block without repacking its uint4/float4 arrays.
struct PsStateCB
{
    uint32_t boolConsts[8];
    uint32_t loopConsts[32];
    uint32_t texInfo[32][4];
    float texScale[32][4];
    uint32_t texSampler[32][4];
    float alphaRef;
    uint32_t alphaTest, alphaFunc, pad;
};
static_assert(offsetof(PsStateCB, loopConsts) == 32);
static_assert(offsetof(PsStateCB, texInfo) == 160);
static_assert(offsetof(PsStateCB, texScale) == 672);
static_assert(offsetof(PsStateCB, texSampler) == 1184);
static_assert(offsetof(PsStateCB, alphaRef) == 1696);
static_assert(sizeof(PsStateCB) == 1712);

// Byte layout of XenosVsState at HLSL register b1.
struct VsStateCB
{
    uint32_t boolConsts[8];
    uint32_t loopConsts[32];
    int32_t vfRegion[96][4];
    uint32_t vte;
    float xs, xo, ys;
    float yo, zs, zo, pitch;
    float rows;
    uint32_t noClip, pad[2];
    uint32_t vfDesc[32][4];
};
static_assert(offsetof(VsStateCB, loopConsts) == 32);
static_assert(offsetof(VsStateCB, vfRegion) == 160);
static_assert(offsetof(VsStateCB, vte) == 1696);
static_assert(offsetof(VsStateCB, yo) == 1712);
static_assert(offsetof(VsStateCB, rows) == 1728);
static_assert(offsetof(VsStateCB, vfDesc) == 1744);
static_assert(sizeof(VsStateCB) == 1744 + 32 * 16);

enum class Mode { Off = 0, On = 1, Compare = 2 };

// The selected backend owns the cached shader. The frontend may only pass this
// handle back to that backend; it never inspects or deletes it.
struct VertexShaderHandle { protected: ~VertexShaderHandle() = default; };
enum class GpuVsResult { Drawn, Skipped, Fallback };

struct DrawState
{
    // colorBase: EDRAM base (RB_COLOR_INFO bits 0-11) | colour format (bits 16-19) << 12.
    uint32_t pitch = 0, sampX = 1, sampY = 1, colorBase = 0, depthBase = 0, writeMask = 0;
    bool zEnable = false, zWrite = false;
    uint32_t zFunc = 7;
    uint32_t blend = 0x00010001;
    float blendConst[4] = { 0, 0, 0, 0 };
    int scX0 = 0, scY0 = 0, scX1 = 0, scY1 = 0;
    bool alphaTest = false;
    uint32_t alphaFunc = 7;
    float alphaRef = 0;
    uint32_t cull = 0;      // PA_SU_SC_MODE_CNTL bits 0/1: cull front/back
    bool frontIsCW = false; // bit 2: clockwise on screen is the front face
    bool depthClip = false; // PA_CL_CLIP_CNTL: clip 0 <= z <= w
    // RB_DEPTHCONTROL and the front/back stencil reference/mask registers.
    bool stencil = false;
    uint32_t depthCtl = 0, stencilRefMask = 0, stencilRefMaskBf = 0;
};

// Convert the frontend's window coordinates into the shared GPU vertex.
// D3D11 uploads a compact prefix; Vulkan uploads the full HwVertex.
// A negative W with clipping disabled keeps the guest's NDC position while
// using positive clip W, matching the existing hardware renderer.
inline HwVertex ConvertCpuVertex(const CpuVertex& vertex,
                                 const DrawState& state, uint32_t interpolators)
{
    HwVertex out{};
    const float w = vertex.invW != 0.0f ? 1.0f / vertex.invW : 1.0f;
    const float clipW = (w < 0.0f && !state.depthClip) ? -w : w;
    const float logicalHeight = float(SurfaceH / state.sampY);
    out.pos[0] = (vertex.x / float(state.pitch) * 2.0f - 1.0f) * clipW;
    out.pos[1] = (1.0f - vertex.y / logicalHeight * 2.0f) * clipW;
    out.pos[2] = vertex.z * clipW;
    out.pos[3] = clipW;
    for (uint32_t attr = 0; attr < interpolators; ++attr)
        for (uint32_t component = 0; component < 4; ++component)
            out.attr[attr][component] = vertex.attr[attr][component] * w;
    return out;
}

} // namespace ttt2_hw
