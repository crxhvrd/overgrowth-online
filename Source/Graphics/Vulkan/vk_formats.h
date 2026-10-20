//-----------------------------------------------------------------------------
//           Name: vk_formats.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: GL texture formats on Vulkan (stage 5.3 of docs_gl46_plan.md):
//                 each GL internal format the engine makes textures with, as a
//                 VkFormat (3-channel ones padded to 4: Vulkan doesn't promise
//                 RGB8 or RGB32F for sampling), and the engine's pixel data (GL
//                 format and type) converted to it, and back for readbacks
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <volk.h>

#include <opengl.h>

#include <cstddef>
#include <cstdint>

namespace VkFormats {

enum Kind {
    kColor,       // float, normalized or sRGB colour
    kColorUint,   // integer colour
    kDepth,
    kCompressed,  // 4x4 blocks
};

struct Info {
    VkFormat format = VK_FORMAT_UNDEFINED;
    Kind kind = kColor;
    uint32_t texel_bytes = 0;  // per texel (per 4x4 block when compressed)
    uint32_t channels = 0;     // in the Vulkan format
    bool padded = false;       // GL's 3 channels stored as 4 (alpha 1)
    bool half = false;         // 16-bit float channels
    bool srgb = false;
};

// The Vulkan format for a GL internal format, sized or not (unsized ones as GL's
// SizedFormat in textures.cpp turns them). UNDEFINED: not one the engine uses
// (logged once). Depth formats the device can't use fall back to D32
Info ForGL(GLenum internal_format);

// Bytes a level or region of w x h x d takes in the format
size_t Size(const Info& info, uint32_t w, uint32_t h, uint32_t d);

// The engine's pixels (GL format/type, rows packed at 4-byte alignment like GL's
// default unpack) as w x h x d texels of info's format, written to out (Size bytes).
// False: a combination the engine doesn't use (logged once; out is zeroed)
bool Convert(const Info& info, GLenum format, GLenum type, const void* pixels, uint32_t w, uint32_t h, uint32_t d, void* out);

// The other way, for readbacks: texels of info's format to GL format/type (rows
// at GL's default 4-byte pack alignment). False: not supported (out is zeroed)
bool ConvertBack(const Info& info, const void* texels, uint32_t w, uint32_t h, uint32_t d, GLenum format, GLenum type, void* out, size_t out_size);

// Bytes GL writes or reads for w x h x d pixels of format/type (4-byte row alignment)
size_t ClientSize(GLenum format, GLenum type, uint32_t w, uint32_t h, uint32_t d);

}  // namespace VkFormats
