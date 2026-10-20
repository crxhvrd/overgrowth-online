//-----------------------------------------------------------------------------
//           Name: vk_formats.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_formats.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "vk_formats.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Logging/logdata.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace VkFormats {
namespace {

void LogOnce(const char* what, GLenum a, GLenum b = 0, GLenum c = 0) {
    static std::mutex mutex;
    static std::set<std::pair<std::string, uint64_t> > seen;
    std::lock_guard<std::mutex> lock(mutex);
    uint64_t key = ((uint64_t)a << 40) ^ ((uint64_t)b << 20) ^ (uint64_t)c;
    if (seen.insert(std::make_pair(std::string(what), key)).second) {
        LOGE << "Vulkan formats: " << what << " (0x" << std::hex << a << ", 0x" << b << ", 0x" << c << std::dec << ")" << std::endl;
    }
}

Info Make(VkFormat format, Kind kind, uint32_t texel_bytes, uint32_t channels, bool padded = false, bool half = false, bool srgb = false) {
    Info i;
    i.format = format;
    i.kind = kind;
    i.texel_bytes = texel_bytes;
    i.channels = channels;
    i.padded = padded;
    i.half = half;
    i.srgb = srgb;
    return i;
}

bool DepthUsable(VkFormat format) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(VkBackend::PhysicalDevice(), format, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 &&
           (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

// ---- half floats (round to nearest even; infinities and NaN kept)

uint16_t FloatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t exp = (x >> 23) & 0xffu;
    uint32_t mant = x & 0x7fffffu;
    if (exp == 0xff) {
        return (uint16_t)(sign | 0x7c00u | (mant ? 0x200u : 0u));
    }
    int e = (int)exp - 127 + 15;
    if (e >= 31) {
        return (uint16_t)(sign | 0x7c00u);
    }
    if (e <= 0) {
        if (e < -10) {
            return (uint16_t)sign;
        }
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - e);
        uint32_t half_mant = mant >> shift;
        uint32_t rest = mant & ((1u << shift) - 1u);
        uint32_t halfway = 1u << (shift - 1);
        if (rest > halfway || (rest == halfway && (half_mant & 1u))) {
            ++half_mant;
        }
        return (uint16_t)(sign | half_mant);
    }
    uint32_t half = sign | ((uint32_t)e << 10) | (mant >> 13);
    uint32_t rest = mant & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u))) {
        ++half;  // (may carry into the exponent: correct, up to infinity)
    }
    return (uint16_t)half;
}

float HalfToFloat(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1fu;
    uint32_t mant = (uint32_t)h & 0x3ffu;
    uint32_t x;
    if (exp == 0) {
        if (mant == 0) {
            x = sign;
        } else {
            int e = -1;
            do {
                ++e;
                mant <<= 1;
            } while ((mant & 0x400u) == 0);
            x = sign | ((uint32_t)(127 - 15 - e) << 23) | ((mant & 0x3ffu) << 13);
        }
    } else if (exp == 31) {
        x = sign | 0x7f800000u | (mant << 13);
    } else {
        x = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &x, 4);
    return f;
}

// ---- the engine's side: GL formats and types

// Components in memory order: R G B A = 0 1 2 3, -1 none
struct Client {
    int count = 0;
    int order[4] = {-1, -1, -1, -1};
    bool integer = false;
};

bool ClientLayout(GLenum format, Client& c) {
    switch (format) {
        case GL_RED: c.count = 1; c.order[0] = 0; return true;
        case GL_RED_INTEGER: c.count = 1; c.order[0] = 0; c.integer = true; return true;
        case GL_DEPTH_COMPONENT: c.count = 1; c.order[0] = 0; return true;
        case GL_RG: c.count = 2; c.order[0] = 0; c.order[1] = 1; return true;
        case GL_RG_INTEGER: c.count = 2; c.order[0] = 0; c.order[1] = 1; c.integer = true; return true;
        case GL_RGB: c.count = 3; c.order[0] = 0; c.order[1] = 1; c.order[2] = 2; return true;
        case GL_BGR: c.count = 3; c.order[0] = 2; c.order[1] = 1; c.order[2] = 0; return true;
        case GL_RGBA: c.count = 4; c.order[0] = 0; c.order[1] = 1; c.order[2] = 2; c.order[3] = 3; return true;
        case GL_BGRA: c.count = 4; c.order[0] = 2; c.order[1] = 1; c.order[2] = 0; c.order[3] = 3; return true;
        case GL_RGBA_INTEGER: c.count = 4; c.order[0] = 0; c.order[1] = 1; c.order[2] = 2; c.order[3] = 3; c.integer = true; return true;
        default: return false;
    }
}

// Bytes per component; 8_8_8_8 types: per component of the packed word
uint32_t ComponentBytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
            return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:
            return 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:
            return 4;
        default:
            return 0;
    }
}

// One component of the engine's pixel as a float (normalized types to [0, 1]) or,
// for integer formats, its value
float ReadComponent(GLenum type, const uint8_t* p) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
            return p[0] / 255.0f;
        case GL_BYTE:
            return std::max(-1.0f, (int8_t)p[0] / 127.0f);
        case GL_UNSIGNED_SHORT: {
            uint16_t v;
            memcpy(&v, p, 2);
            return v / 65535.0f;
        }
        case GL_HALF_FLOAT: {
            uint16_t v;
            memcpy(&v, p, 2);
            return HalfToFloat(v);
        }
        case GL_FLOAT: {
            float v;
            memcpy(&v, p, 4);
            return v;
        }
        default:
            return 0.0f;
    }
}

uint32_t ReadInteger(GLenum type, const uint8_t* p) {
    switch (type) {
        case GL_UNSIGNED_BYTE: return p[0];
        case GL_UNSIGNED_SHORT: {
            uint16_t v;
            memcpy(&v, p, 2);
            return v;
        }
        case GL_UNSIGNED_INT:
        case GL_INT: {
            uint32_t v;
            memcpy(&v, p, 4);
            return v;
        }
        default: return 0;
    }
}

void WriteComponent(GLenum type, float v, uint8_t* p) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV: {
            float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            p[0] = (uint8_t)(c * 255.0f + 0.5f);
            break;
        }
        case GL_UNSIGNED_SHORT: {
            float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            uint16_t s = (uint16_t)(c * 65535.0f + 0.5f);
            memcpy(p, &s, 2);
            break;
        }
        case GL_HALF_FLOAT: {
            uint16_t h = FloatToHalf(v);
            memcpy(p, &h, 2);
            break;
        }
        case GL_FLOAT:
            memcpy(p, &v, 4);
            break;
        default:
            break;
    }
}

// ---- the Vulkan side: a texel as four floats (or integers)

void ReadTexel(const Info& info, const uint8_t* p, float rgba[4], uint32_t urgba[4]) {
    rgba[0] = rgba[1] = rgba[2] = 0.0f;
    rgba[3] = 1.0f;
    urgba[0] = urgba[1] = urgba[2] = 0;
    urgba[3] = 1;
    switch (info.format) {
        case VK_FORMAT_X8_D24_UNORM_PACK32: {
            uint32_t v;
            memcpy(&v, p, 4);
            rgba[0] = (v & 0xffffffu) / 16777215.0f;
            return;
        }
        case VK_FORMAT_D16_UNORM: {
            uint16_t v;
            memcpy(&v, p, 2);
            rgba[0] = v / 65535.0f;
            return;
        }
        default:
            break;
    }
    const uint32_t comp_bytes = info.texel_bytes / info.channels;
    for (uint32_t c = 0; c < info.channels; ++c) {
        const uint8_t* q = p + c * comp_bytes;
        if (info.kind == kColorUint) {
            urgba[c] = ReadInteger(comp_bytes == 4 ? GL_UNSIGNED_INT : comp_bytes == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, q);
        } else if (comp_bytes == 1) {
            rgba[c] = q[0] / 255.0f;
        } else if (comp_bytes == 2) {
            uint16_t v;
            memcpy(&v, q, 2);
            rgba[c] = info.half ? HalfToFloat(v) : v / 65535.0f;
        } else {
            memcpy(&rgba[c], q, 4);
        }
    }
}

void WriteTexel(const Info& info, const float rgba[4], const uint32_t urgba[4], uint8_t* p) {
    switch (info.format) {
        case VK_FORMAT_X8_D24_UNORM_PACK32: {
            float c = rgba[0] < 0.0f ? 0.0f : rgba[0] > 1.0f ? 1.0f : rgba[0];
            uint32_t v = (uint32_t)(c * 16777215.0f + 0.5f);
            memcpy(p, &v, 4);
            return;
        }
        case VK_FORMAT_D16_UNORM: {
            float c = rgba[0] < 0.0f ? 0.0f : rgba[0] > 1.0f ? 1.0f : rgba[0];
            uint16_t v = (uint16_t)(c * 65535.0f + 0.5f);
            memcpy(p, &v, 2);
            return;
        }
        default:
            break;
    }
    const uint32_t comp_bytes = info.texel_bytes / info.channels;
    for (uint32_t c = 0; c < info.channels; ++c) {
        uint8_t* q = p + c * comp_bytes;
        if (info.kind == kColorUint) {
            if (comp_bytes == 4) {
                memcpy(q, &urgba[c], 4);
            } else if (comp_bytes == 2) {
                uint16_t v = (uint16_t)urgba[c];
                memcpy(q, &v, 2);
            } else {
                q[0] = (uint8_t)urgba[c];
            }
        } else if (comp_bytes == 1) {
            WriteComponent(GL_UNSIGNED_BYTE, rgba[c], q);
        } else if (comp_bytes == 2) {
            WriteComponent(info.half ? GL_HALF_FLOAT : GL_UNSIGNED_SHORT, rgba[c], q);
        } else {
            memcpy(q, &rgba[c], 4);
        }
    }
}

size_t ClientPitch(const Client& c, GLenum type, uint32_t w) {
    size_t row = (size_t)w * c.count * ComponentBytes(type);
    return (row + 3) & ~(size_t)3;  // (GL_UNPACK_ALIGNMENT / GL_PACK_ALIGNMENT 4: never changed)
}

}  // namespace

Info ForGL(GLenum internal_format) {
    switch (internal_format) {
        case GL_RGBA8:
        case GL_RGBA:
            return Make(VK_FORMAT_R8G8B8A8_UNORM, kColor, 4, 4);
        case GL_RGB8:
        case GL_RGB:
            return Make(VK_FORMAT_R8G8B8A8_UNORM, kColor, 4, 4, true);
        case GL_SRGB8_ALPHA8:
        case GL_SRGB_ALPHA:
            return Make(VK_FORMAT_R8G8B8A8_SRGB, kColor, 4, 4, false, false, true);
        case GL_SRGB8:
        case GL_SRGB:
            return Make(VK_FORMAT_R8G8B8A8_SRGB, kColor, 4, 4, true, false, true);
        case GL_R8:
            return Make(VK_FORMAT_R8_UNORM, kColor, 1, 1);
        case GL_RG8:
            return Make(VK_FORMAT_R8G8_UNORM, kColor, 2, 2);
        case GL_RGBA16:
            return Make(VK_FORMAT_R16G16B16A16_UNORM, kColor, 8, 4);
        case GL_RGBA16F:
            return Make(VK_FORMAT_R16G16B16A16_SFLOAT, kColor, 8, 4, false, true);
        case GL_RGB16F:
            return Make(VK_FORMAT_R16G16B16A16_SFLOAT, kColor, 8, 4, true, true);
        case GL_RG16F:
            return Make(VK_FORMAT_R16G16_SFLOAT, kColor, 4, 2, false, true);
        case GL_R16F:
            return Make(VK_FORMAT_R16_SFLOAT, kColor, 2, 1, false, true);
        case GL_RGBA32F:
            return Make(VK_FORMAT_R32G32B32A32_SFLOAT, kColor, 16, 4);
        case GL_RGB32F:
            return Make(VK_FORMAT_R32G32B32A32_SFLOAT, kColor, 16, 4, true);
        case GL_RG32F:
            return Make(VK_FORMAT_R32G32_SFLOAT, kColor, 8, 2);
        case GL_R32F:
            return Make(VK_FORMAT_R32_SFLOAT, kColor, 4, 1);
        case GL_R32UI:
            return Make(VK_FORMAT_R32_UINT, kColorUint, 4, 1);
        case GL_RG32UI:
            return Make(VK_FORMAT_R32G32_UINT, kColorUint, 8, 2);
        case GL_RGBA32UI:
            return Make(VK_FORMAT_R32G32B32A32_UINT, kColorUint, 16, 4);
        case GL_DEPTH_COMPONENT32F:
        case GL_DEPTH_COMPONENT32:
            return Make(VK_FORMAT_D32_SFLOAT, kDepth, 4, 1);
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH_COMPONENT: {
            static const bool d24 = DepthUsable(VK_FORMAT_X8_D24_UNORM_PACK32);  // (not on AMD)
            return d24 ? Make(VK_FORMAT_X8_D24_UNORM_PACK32, kDepth, 4, 1) : Make(VK_FORMAT_D32_SFLOAT, kDepth, 4, 1);
        }
        case GL_DEPTH_COMPONENT16:
            return Make(VK_FORMAT_D16_UNORM, kDepth, 2, 1);
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
            return Make(VK_FORMAT_BC1_RGB_UNORM_BLOCK, kCompressed, 8, 3);
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            return Make(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, kCompressed, 8, 4);
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            return Make(VK_FORMAT_BC2_UNORM_BLOCK, kCompressed, 16, 4);
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            return Make(VK_FORMAT_BC3_UNORM_BLOCK, kCompressed, 16, 4);
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
            return Make(VK_FORMAT_BC1_RGB_SRGB_BLOCK, kCompressed, 8, 3, false, false, true);
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            return Make(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, kCompressed, 8, 4, false, false, true);
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
            return Make(VK_FORMAT_BC2_SRGB_BLOCK, kCompressed, 16, 4, false, false, true);
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return Make(VK_FORMAT_BC3_SRGB_BLOCK, kCompressed, 16, 4, false, false, true);
        default:
            LogOnce("internal format not mapped", internal_format);
            return Info();
    }
}

size_t Size(const Info& info, uint32_t w, uint32_t h, uint32_t d) {
    if (info.kind == kCompressed) {
        return (size_t)((w + 3) / 4) * ((h + 3) / 4) * d * info.texel_bytes;
    }
    return (size_t)w * h * d * info.texel_bytes;
}

size_t ClientSize(GLenum format, GLenum type, uint32_t w, uint32_t h, uint32_t d) {
    Client c;
    if (!ClientLayout(format, c) || ComponentBytes(type) == 0 || w == 0 || h == 0 || d == 0) {
        return 0;
    }
    size_t pitch = ClientPitch(c, type, w);
    return pitch * h * d - (pitch - (size_t)w * c.count * ComponentBytes(type));  // (the last row unpadded, as GL reads it)
}

bool Convert(const Info& info, GLenum format, GLenum type, const void* pixels, uint32_t w, uint32_t h, uint32_t d, void* out) {
    const size_t out_size = Size(info, w, h, d);
    if (info.format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    if (info.kind == kCompressed) {
        LogOnce("uncompressed data for a compressed format", info.format, format, type);
        memset(out, 0, out_size);
        return false;
    }
    Client c;
    const uint32_t comp_bytes = ComponentBytes(type);
    if (!ClientLayout(format, c) || comp_bytes == 0 || c.integer != (info.kind == kColorUint)) {
        LogOnce("pixel format/type not converted", info.format, format, type);
        memset(out, 0, out_size);
        return false;
    }
    const uint8_t* src = (const uint8_t*)pixels;
    uint8_t* dst = (uint8_t*)out;
    const size_t pitch = ClientPitch(c, type, w);
    const size_t dst_row = (size_t)w * info.texel_bytes;
    const bool bytes = type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV;
    for (uint32_t z = 0; z < d; ++z) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + ((size_t)z * h + y) * pitch;
            uint8_t* o = dst + ((size_t)z * h + y) * dst_row;
            // The common cases without the per-component path: 8-bit RGBA and BGRA,
            // RGB padded, float RGBA as is
            if (bytes && info.texel_bytes == 4 && info.channels == 4 && info.kind == kColor) {
                if (format == GL_RGBA) {
                    memcpy(o, s, dst_row);
                    continue;
                }
                if (format == GL_BGRA) {
                    for (uint32_t x = 0; x < w; ++x) {
                        o[x * 4 + 0] = s[x * 4 + 2];
                        o[x * 4 + 1] = s[x * 4 + 1];
                        o[x * 4 + 2] = s[x * 4 + 0];
                        o[x * 4 + 3] = s[x * 4 + 3];
                    }
                    continue;
                }
                if (format == GL_RGB) {
                    for (uint32_t x = 0; x < w; ++x) {
                        o[x * 4 + 0] = s[x * 3 + 0];
                        o[x * 4 + 1] = s[x * 3 + 1];
                        o[x * 4 + 2] = s[x * 3 + 2];
                        o[x * 4 + 3] = 255;
                    }
                    continue;
                }
            }
            if (type == GL_FLOAT && format == GL_RGBA && info.texel_bytes == 16 && info.channels == 4 && info.kind == kColor) {
                memcpy(o, s, dst_row);
                continue;
            }
            if (((type == GL_UNSIGNED_INT && info.kind == kColorUint) || (type == GL_FLOAT && info.kind != kColorUint && !info.half)) &&
                (size_t)c.count * 4 == info.texel_bytes && c.count == (int)info.channels && (c.count == 1 || format == GL_RGBA || format == GL_RG ||
                                                                                         format == GL_RGBA_INTEGER || format == GL_RG_INTEGER) &&
                info.format != VK_FORMAT_X8_D24_UNORM_PACK32) {
                memcpy(o, s, dst_row);
                continue;
            }
            const size_t pixel = (size_t)c.count * comp_bytes;
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* p = s + x * pixel;
                float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                uint32_t urgba[4] = {0, 0, 0, 1};
                for (int k = 0; k < c.count; ++k) {
                    // (8_8_8_8 non-REV: the packed word's bytes are reversed in memory)
                    const int at = type == GL_UNSIGNED_INT_8_8_8_8 ? (c.count - 1 - k) : k;
                    const uint8_t* q = p + at * comp_bytes;
                    if (c.integer) {
                        urgba[c.order[k]] = ReadInteger(type, q);
                    } else {
                        rgba[c.order[k]] = ReadComponent(type, q);
                    }
                }
                WriteTexel(info, rgba, urgba, o + x * info.texel_bytes);
            }
        }
    }
    return true;
}

bool ConvertBack(const Info& info, const void* texels, uint32_t w, uint32_t h, uint32_t d, GLenum format, GLenum type, void* out, size_t out_size) {
    memset(out, 0, out_size);
    Client c;
    const uint32_t comp_bytes = ComponentBytes(type);
    if (info.kind == kCompressed || !ClientLayout(format, c) || comp_bytes == 0 || c.integer != (info.kind == kColorUint)) {
        LogOnce("readback format/type not converted", info.format, format, type);
        return false;
    }
    const size_t pitch = ClientPitch(c, type, w);
    if (ClientSize(format, type, w, h, d) > out_size) {
        LogOnce("readback buffer too small", info.format, format, type);
        return false;
    }
    const uint8_t* src = (const uint8_t*)texels;
    uint8_t* dst = (uint8_t*)out;
    const size_t src_row = (size_t)w * info.texel_bytes;
    const size_t pixel = (size_t)c.count * comp_bytes;
    for (uint32_t z = 0; z < d; ++z) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + ((size_t)z * h + y) * src_row;
            uint8_t* o = dst + ((size_t)z * h + y) * pitch;
            if ((type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV) && format == GL_RGBA && info.texel_bytes == 4 &&
                info.channels == 4 && info.kind == kColor) {
                memcpy(o, s, src_row);
                continue;
            }
            for (uint32_t x = 0; x < w; ++x) {
                float rgba[4];
                uint32_t urgba[4];
                ReadTexel(info, s + x * info.texel_bytes, rgba, urgba);
                uint8_t* p = o + x * pixel;
                for (int k = 0; k < c.count; ++k) {
                    const int at = type == GL_UNSIGNED_INT_8_8_8_8 ? (c.count - 1 - k) : k;
                    uint8_t* q = p + at * comp_bytes;
                    if (c.integer) {
                        uint32_t v = urgba[c.order[k]];
                        if (comp_bytes == 4) {
                            memcpy(q, &v, 4);
                        } else if (comp_bytes == 2) {
                            uint16_t v16 = (uint16_t)v;
                            memcpy(q, &v16, 2);
                        } else {
                            q[0] = (uint8_t)v;
                        }
                    } else {
                        WriteComponent(type, rgba[c.order[k]], q);
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace VkFormats
