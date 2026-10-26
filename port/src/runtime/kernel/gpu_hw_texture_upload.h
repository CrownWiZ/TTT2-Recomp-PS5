#pragma once

#include "gpu_hw_api.h"

#include <cstdint>
#include <vector>

namespace ttt2_hw {

enum class TextureUploadFormat
{
    Rgba8,
    Bgra8,
    R8,
    Bc1,
    Bc2,
    Bc3,
    Rgba32Float
};

// One decoded 2D surface or cube face. Data is tightly row packed in the
// selected host format. Generic formats have already passed through the CPU
// sampler's swizzle and use its result as RGBA32F.
struct TextureUploadSurface
{
    TextureUploadFormat format = TextureUploadFormat::Rgba8;
    uint32_t width = 0, height = 0, rowPitch = 0;
    uint32_t remapMode = 0;
    bool genericSwizzled = false;
    std::vector<uint8_t> bytes;
};

// Uses the frontend's guest-memory access and texture decoder; no host GPU API
// is involved. The caller supplies a face-adjusted TextureInfo for cube maps.
bool DecodeTextureUploadSurface(const TextureInfo& texture,
                                TextureUploadSurface& out);
bool DecodeTextureUploadFaces(const TextureInfo& texture, uint32_t layers,
                              std::vector<TextureUploadSurface>& out);
// Decode a raw guest front buffer into B8G8R8A8 bytes for presentation.
// Unlike the general texture decoder, the guest-memory base is explicit.
bool DecodeFrontBufferUploadSurface(uint8_t* guestMemory,
                                    uint32_t guestAddress,
                                    uint32_t width, uint32_t height,
                                    TextureUploadSurface& out);
uint32_t TextureGuestBytes(const TextureInfo& texture, uint32_t layers);

} // namespace ttt2_hw
