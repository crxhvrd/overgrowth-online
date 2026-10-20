//-----------------------------------------------------------------------------
//           Name: vk_gl.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_gl.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>
#include <vk_mem_alloc.h>

#include "vk_gl.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_formats.h>
#include <Logging/logdata.h>
#include <Internal/config.h>
#include <Internal/error.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace vkgl {
namespace {

void LogOnce(const std::string& what) {
    static std::set<std::string> seen;
    if (seen.insert(what).second) {
        LOGE << "Vulkan resources: " << what << std::endl;
    }
}

// The command buffer for a transfer or a layout change: a pass being drawn ends first
// (a transfer can't happen inside one), except for the pass's own attachments
bool pass_setup = false;
VkCommandBuffer Cmd() {
    return pass_setup ? VkBackend::Commands() : VkBackend::TransferCommands();
}

// ---- staging memory: chunks recycled once the GPU is done with them

struct Chunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    uint8_t* ptr = NULL;
    VkDeviceSize size = 0;
    VkDeviceSize used = 0;
    uint64_t serial = 0;  // the last submit that reads it
};

struct Staging {
    VkBuffer buffer;
    VkDeviceSize offset;
    uint8_t* ptr;
};

const VkDeviceSize kChunkSize = 16u << 20;
const VkDeviceSize kKeepFree = 64u << 20;       // free chunks kept for reuse
const VkDeviceSize kBusyBeforeWait = 256u << 20;  // uploads in flight before waiting for the GPU

Chunk current_chunk;
std::deque<Chunk> busy_chunks;
std::vector<Chunk> free_chunks;
VkDeviceSize busy_bytes = 0;

bool MakeChunk(VkDeviceSize size, Chunk* out) {
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai = {};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo got;
    if (vmaCreateBuffer(VkBackend::Allocator(), &info, &ai, &out->buffer, &out->alloc, &got) != VK_SUCCESS) {
        return false;
    }
    out->ptr = (uint8_t*)got.pMappedData;
    out->size = size;
    out->used = 0;
    out->serial = 0;
    return true;
}

void DestroyChunk(Chunk& c) {
    if (c.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VkBackend::Allocator(), c.buffer, c.alloc);
    }
    c = Chunk();
}

void RecycleChunks() {
    const uint64_t done = VkBackend::CompletedSerial();
    while (!busy_chunks.empty() && busy_chunks.front().serial <= done) {
        Chunk c = busy_chunks.front();
        busy_chunks.pop_front();
        busy_bytes -= c.size;
        VkDeviceSize kept = 0;
        for (const Chunk& f : free_chunks) {
            kept += f.size;
        }
        if (c.size != kChunkSize || kept >= kKeepFree) {
            DestroyChunk(c);  // (one made for a big upload, or enough kept)
        } else {
            c.used = 0;
            free_chunks.push_back(c);
        }
    }
}

// Room for size bytes, written by the CPU now and read by the commands recorded next
Staging Stage(VkDeviceSize size) {
    const VkDeviceSize align = 16;  // (any texel block, and 4 for depth copies)
    VkDeviceSize at = (current_chunk.used + align - 1) / align * align;
    if (current_chunk.buffer == VK_NULL_HANDLE || at + size > current_chunk.size) {
        if (current_chunk.buffer != VK_NULL_HANDLE) {
            busy_bytes += current_chunk.size;
            busy_chunks.push_back(current_chunk);
            current_chunk = Chunk();
        }
        RecycleChunks();
        if (busy_bytes > kBusyBeforeWait && !busy_chunks.empty()) {
            // (a level load: let the GPU catch up instead of holding gigabytes)
            VkBackend::WaitSerial(busy_chunks.front().serial);
            RecycleChunks();
        }
        if (size <= kChunkSize && !free_chunks.empty()) {
            current_chunk = free_chunks.back();
            free_chunks.pop_back();
        } else if (!MakeChunk(std::max(size, kChunkSize), &current_chunk)) {
            FatalError("Error", "Vulkan: out of memory for staging %u bytes", (unsigned)size);
        }
        at = 0;
    }
    current_chunk.used = at + size;
    current_chunk.serial = VkBackend::RecordingSerial();
    Staging s = {current_chunk.buffer, at, current_chunk.ptr + at};
    return s;
}

// ---- barriers

struct Access {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

Access AccessOf(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return {VK_PIPELINE_STAGE_2_NONE, 0};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            // (and the colour-output stage: a pass's depth resolve, and its store with one,
            // happen there with colour-attachment writes)
            return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        default:
            return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
}

// A transfer into or out of a buffer, ordered with everything around it (GL's
// buffer updates are)
void BufferTransferBarrier(bool before) {
    VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    if (before) {
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    } else {
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    VkDependencyInfo d = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.memoryBarrierCount = 1;
    d.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(Cmd(), &d);
}

// ---- textures

struct Storage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkFormats::Info fmt;
    GLenum gl_format = 0;
    VkImageType type = VK_IMAGE_TYPE_2D;
    uint32_t width = 0, height = 0, depth = 1, layers = 1, levels = 1, samples = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::vector<VkImageLayout> layouts;  // [layer * levels + level]
    size_t bytes = 0;

    ~Storage() {
        if (image != VK_NULL_HANDLE) {
            VkImage i = image;
            VmaAllocation a = alloc;
            VkBackend::DeferDestroy([i, a]() { vmaDestroyImage(VkBackend::Allocator(), i, a); });
        }
    }
    uint32_t MipWidth(uint32_t level) const { return std::max(1u, width >> level); }
    uint32_t MipHeight(uint32_t level) const { return std::max(1u, height >> level); }
    uint32_t MipDepth(uint32_t level) const { return type == VK_IMAGE_TYPE_3D ? std::max(1u, depth >> level) : 1u; }
};

struct Texture {
    GLenum target = 0;
    std::shared_ptr<Storage> storage;
    // The storage range this texture (or view) covers
    uint32_t base_level = 0, num_levels = 0, base_layer = 0, num_layers = 0;
    GLenum view_format = 0;
    VkImageView view = VK_NULL_HANDLE;  // (made when first asked for)
    // A buffer texture
    GLuint buffer = 0;
    GLenum buffer_format = 0;
    VkBufferView buffer_view = VK_NULL_HANDLE;
    VkBuffer buffer_view_of = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkImageView> attachment_views;  // (level << 32 | layer)

    void DestroyViews() {
        for (auto& kv : attachment_views) {
            VkImageView v = kv.second;
            VkBackend::DeferDestroy([v]() { vkDestroyImageView(VkBackend::Device(), v, NULL); });
        }
        attachment_views.clear();
        if (view != VK_NULL_HANDLE) {
            VkImageView v = view;
            VkBackend::DeferDestroy([v]() { vkDestroyImageView(VkBackend::Device(), v, NULL); });
            view = VK_NULL_HANDLE;
        }
        if (buffer_view != VK_NULL_HANDLE) {
            VkBufferView v = buffer_view;
            VkBackend::DeferDestroy([v]() { vkDestroyBufferView(VkBackend::Device(), v, NULL); });
            buffer_view = VK_NULL_HANDLE;
        }
    }
};

// (the tables are never destroyed: the engine's static destructors may still call in at
// exit, after the device is gone; `shut_down` turns those calls away)
std::unordered_map<GLuint, Texture>& textures = *new std::unordered_map<GLuint, Texture>();
bool shut_down = false;

Texture* FindTexture(GLuint name, const char* what) {
    auto it = textures.find(name);
    if (it == textures.end()) {
        LogOnce(std::string(what) + ": no such texture");
        return NULL;
    }
    return &it->second;
}

// The texture's storage, for an operation on it (false: none yet)
Storage* StorageOf(Texture* t, const char* what) {
    if (t == NULL) {
        return NULL;
    }
    if (!t->storage || t->storage->image == VK_NULL_HANDLE) {
        LogOnce(std::string(what) + ": a texture with no storage");
        return NULL;
    }
    return t->storage.get();
}

// Subresources [levels) x [layers) to layout, one barrier per run that shares a layout
void Transition(Storage& s, uint32_t base_level, uint32_t num_levels, uint32_t base_layer, uint32_t num_layers, VkImageLayout to) {
    std::vector<VkImageMemoryBarrier2> barriers;
    for (uint32_t level = base_level; level < base_level + num_levels; ++level) {
        uint32_t layer = base_layer;
        while (layer < base_layer + num_layers) {
            VkImageLayout from = s.layouts[layer * s.levels + level];
            uint32_t run = 1;
            while (layer + run < base_layer + num_layers && s.layouts[(layer + run) * s.levels + level] == from) {
                ++run;
            }
            if (from != to) {
                Access a = AccessOf(from), b = AccessOf(to);
                VkImageMemoryBarrier2 m = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                m.srcStageMask = a.stage;
                m.srcAccessMask = a.access;
                m.dstStageMask = b.stage;
                m.dstAccessMask = b.access;
                m.oldLayout = from;
                m.newLayout = to;
                m.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                m.image = s.image;
                m.subresourceRange = {s.aspect, level, 1, layer, run};
                barriers.push_back(m);
                for (uint32_t k = 0; k < run; ++k) {
                    s.layouts[(layer + k) * s.levels + level] = to;
                }
            }
            layer += run;
        }
    }
    if (!barriers.empty()) {
        VkDependencyInfo d = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.imageMemoryBarrierCount = (uint32_t)barriers.size();
        d.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(Cmd(), &d);
    }
}

void TransitionAll(Storage& s, VkImageLayout to) {
    Transition(s, 0, s.levels, 0, s.layers, to);
}

GLenum TargetOf(Texture* t) {
    return t->target != 0 ? t->target : GL_TEXTURE_2D;
}

bool IsCubeTarget(GLenum target) {
    return target == GL_TEXTURE_CUBE_MAP || target == GL_TEXTURE_CUBE_MAP_ARRAY;
}

// Storage for a texture: the image, cleared (GL drivers hand out zeroed memory;
// nothing is ever sampled undefined), in its resting layout
void MakeStorage(GLuint name, GLenum target, uint32_t levels, GLenum internalformat, uint32_t w, uint32_t h, uint32_t d, uint32_t samples) {
    Texture* t = FindTexture(name, "TextureStorage");
    if (t == NULL) {
        return;
    }
    if (t->target == 0) {
        t->target = target;
    }
    t->DestroyViews();
    std::shared_ptr<Storage> s(new Storage());
    s->fmt = VkFormats::ForGL(internalformat);
    s->gl_format = internalformat;
    if (s->fmt.format == VK_FORMAT_UNDEFINED || w == 0 || h == 0) {
        t->storage = s;
        return;
    }
    const GLenum tt = t->target;
    s->type = tt == GL_TEXTURE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    s->width = w;
    s->height = h;
    s->depth = s->type == VK_IMAGE_TYPE_3D ? d : 1;
    s->layers = s->type == VK_IMAGE_TYPE_3D ? 1 : (tt == GL_TEXTURE_CUBE_MAP ? 6 : d);
    s->levels = std::max(1u, levels);
    s->samples = std::max(1u, samples);
    const bool depth = s->fmt.kind == VkFormats::kDepth;
    s->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    s->rest = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = s->type;
    info.format = s->fmt.format;
    info.extent = {s->width, s->height, s->depth};
    info.mipLevels = s->levels;
    info.arrayLayers = s->layers;
    info.samples = (VkSampleCountFlagBits)s->samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (IsCubeTarget(tt) && s->layers % 6 == 0 && w == h) {
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    // Any texture may become a render target (GL lets any be attached): attachment
    // usage where the format can be one
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(VkBackend::PhysicalDevice(), s->fmt.format, &fp);
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (depth && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else if (!depth && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    VmaAllocationCreateInfo ai = {};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(VkBackend::Allocator(), &info, &ai, &s->image, &s->alloc, NULL) != VK_SUCCESS) {
        LOGE << "Vulkan: could not make a " << w << "x" << h << "x" << d << " image (format " << (int)s->fmt.format << ")" << std::endl;
        s->image = VK_NULL_HANDLE;
        t->storage = s;
        return;
    }
    s->layouts.assign((size_t)s->levels * s->layers, VK_IMAGE_LAYOUT_UNDEFINED);
    for (uint32_t l = 0; l < s->levels; ++l) {
        s->bytes += VkFormats::Size(s->fmt, s->MipWidth(l), s->MipHeight(l), s->MipDepth(l)) * s->layers * s->samples;
    }
    if (s->fmt.kind != VkFormats::kCompressed) {
        TransitionAll(*s, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageSubresourceRange range = {s->aspect, 0, s->levels, 0, s->layers};
        if (depth) {
            VkClearDepthStencilValue zero = {0.0f, 0};  // (reversed-Z: 0 is far)
            vkCmdClearDepthStencilImage(Cmd(), s->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        } else {
            VkClearColorValue zero;
            memset(&zero, 0, sizeof(zero));
            vkCmdClearColorImage(Cmd(), s->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        }
    }
    TransitionAll(*s, s->rest);
    t->storage = s;
    t->base_level = 0;
    t->num_levels = s->levels;
    t->base_layer = 0;
    t->num_layers = s->layers;
}

uint64_t uploads = 0, upload_bytes = 0;

// A region of one level (of layers [z, z + d) for layered textures, or z for 3D)
// from staging
void CopyIn(Texture* t, Storage& s, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, const Staging& st, size_t bytes) {
    const uint32_t lvl = t->base_level + (uint32_t)level;
    const bool is3d = s.type == VK_IMAGE_TYPE_3D;
    const uint32_t layer = is3d ? 0 : t->base_layer + (uint32_t)z;
    const uint32_t layers = is3d ? 1 : (uint32_t)d;
    if (lvl >= s.levels || layer + layers > s.layers) {
        LogOnce("an upload outside the texture");
        return;
    }
    Transition(s, lvl, 1, layer, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region = {};
    region.bufferOffset = st.offset;
    region.imageSubresource = {s.aspect, lvl, layer, layers};
    region.imageOffset = {x, y, is3d ? z : 0};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, is3d ? (uint32_t)d : 1u};
    vkCmdCopyBufferToImage(Cmd(), st.buffer, s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    Transition(s, lvl, 1, layer, layers, s.rest);
    ++uploads;
    upload_bytes += bytes;
}

void SubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type,
              const void* pixels) {
    Texture* t = FindTexture(texture, "TextureSubImage");
    Storage* s = StorageOf(t, "TextureSubImage");
    if (s == NULL || w <= 0 || h <= 0 || d <= 0) {
        return;
    }
    if (s->fmt.kind == VkFormats::kCompressed) {
        LogOnce("uncompressed pixels for a compressed texture");
        return;
    }
    const size_t bytes = VkFormats::Size(s->fmt, w, h, d);
    Staging st = Stage(bytes);
    if (pixels == NULL) {
        memset(st.ptr, 0, bytes);
    } else {
        VkFormats::Convert(s->fmt, format, type, pixels, w, h, d, st.ptr);
    }
    CopyIn(t, *s, level, x, y, z, w, h, d, st, bytes);
}

void CompressedSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLsizei size,
                        const void* data) {
    Texture* t = FindTexture(texture, "CompressedTextureSubImage");
    Storage* s = StorageOf(t, "CompressedTextureSubImage");
    if (s == NULL || w <= 0 || h <= 0 || d <= 0) {
        return;
    }
    const size_t bytes = VkFormats::Size(s->fmt, w, h, d);
    if (s->fmt.kind != VkFormats::kCompressed || (size_t)size < bytes) {
        LogOnce("compressed data that doesn't fit its texture");
        return;
    }
    Staging st = Stage(bytes);
    if (data == NULL) {
        memset(st.ptr, 0, bytes);
    } else {
        memcpy(st.ptr, data, bytes);
    }
    CopyIn(t, *s, level, x, y, z, w, h, d, st, bytes);
}

// Reads a region back: copied out, waited for, then handed to fill(texels)
template <typename F>
void ReadBack(Texture* t, Storage& s, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, F fill) {
    const uint32_t lvl = t->base_level + (uint32_t)level;
    const bool is3d = s.type == VK_IMAGE_TYPE_3D;
    const uint32_t layer = is3d ? 0 : t->base_layer + (uint32_t)z;
    const uint32_t layers = is3d ? 1 : (uint32_t)d;
    if (lvl >= s.levels || layer + layers > s.layers || s.samples > 1) {
        LogOnce("a readback outside the texture (or multisampled)");
        return;
    }
    const size_t bytes = VkFormats::Size(s.fmt, w, h, d);
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<size_t>(bytes, 16);
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai = {};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer buffer;
    VmaAllocation alloc;
    VmaAllocationInfo got;
    if (vmaCreateBuffer(VkBackend::Allocator(), &info, &ai, &buffer, &alloc, &got) != VK_SUCCESS) {
        LogOnce("no memory for a readback");
        return;
    }
    Transition(s, lvl, 1, layer, layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region = {};
    region.imageSubresource = {s.aspect, lvl, layer, layers};
    region.imageOffset = {x, y, is3d ? z : 0};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, is3d ? (uint32_t)d : 1u};
    vkCmdCopyImageToBuffer(Cmd(), s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    Transition(s, lvl, 1, layer, layers, s.rest);
    VkMemoryBarrier2 host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &host;
    vkCmdPipelineBarrier2(Cmd(), &dep);
    const uint64_t serial = VkBackend::RecordingSerial();
    VkBackend::WaitSerial(serial);
    vmaInvalidateAllocation(VkBackend::Allocator(), alloc, 0, VK_WHOLE_SIZE);
    fill((const uint8_t*)got.pMappedData, bytes);
    vmaDestroyBuffer(VkBackend::Allocator(), buffer, alloc);
}

// ---- buffers

struct Backing {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    uint8_t* mapped = NULL;  // host-visible ones
    VkDeviceSize size = 0;

    ~Backing() {
        if (buffer != VK_NULL_HANDLE) {
            VkBuffer b = buffer;
            VmaAllocation a = alloc;
            VkBackend::DeferDestroy([b, a]() { vmaDestroyBuffer(VkBackend::Allocator(), b, a); });
        }
    }
};

struct BufferObj {
    std::shared_ptr<Backing> backing;
    bool immutable = false;
    bool host = false;  // made host-visible (GL would map it)
    // A mapping in progress: straight into host-visible memory, or staged and copied
    // in at the unmap
    bool mapped = false;
    bool staged = false;
    Staging staging = {VK_NULL_HANDLE, 0, NULL};
    GLintptr map_offset = 0;
    GLsizeiptr map_length = 0;
};

std::unordered_map<GLuint, BufferObj>& buffers = *new std::unordered_map<GLuint, BufferObj>();

BufferObj* FindBuffer(GLuint name, const char* what) {
    auto it = buffers.find(name);
    if (it == buffers.end()) {
        LogOnce(std::string(what) + ": no such buffer");
        return NULL;
    }
    return &it->second;
}

std::shared_ptr<Backing> MakeBacking(VkDeviceSize size, bool host) {
    std::shared_ptr<Backing> b(new Backing());
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(size, 16);  // (Vulkan has no empty buffers)
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai = {};
    if (host) {
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }
    VmaAllocationInfo got;
    if (vmaCreateBuffer(VkBackend::Allocator(), &info, &ai, &b->buffer, &b->alloc, &got) != VK_SUCCESS) {
        FatalError("Error", "Vulkan: out of memory for a %u-byte buffer", (unsigned)size);
    }
    b->mapped = host ? (uint8_t*)got.pMappedData : NULL;
    b->size = size;
    return b;
}

// size bytes into the buffer at offset, in GL's order with the commands around it
void CopyToBuffer(Backing& b, VkDeviceSize offset, const Staging& st, VkDeviceSize size) {
    BufferTransferBarrier(true);
    VkBufferCopy region = {st.offset, offset, size};
    vkCmdCopyBuffer(Cmd(), st.buffer, b.buffer, 1, &region);
    BufferTransferBarrier(false);
    ++uploads;
    upload_bytes += size;
}

void Fill(BufferObj& o, const void* data, VkDeviceSize size) {
    if (data == NULL || size == 0) {
        return;
    }
    if (o.backing->mapped != NULL) {
        memcpy(o.backing->mapped, data, size);  // (new memory: nothing reads it yet)
    } else {
        Staging st = Stage(size);
        memcpy(st.ptr, data, size);
        CopyToBuffer(*o.backing, 0, st, size);
    }
}

// ---- samplers

struct SamplerObj {
    GLint min_filter = GL_NEAREST_MIPMAP_LINEAR, mag_filter = GL_LINEAR;
    GLint wrap_s = GL_REPEAT, wrap_t = GL_REPEAT, wrap_r = GL_REPEAT;
    GLint compare_mode = GL_NONE, compare_func = GL_LEQUAL;
    float min_lod = -1000.0f, max_lod = 1000.0f, lod_bias = 0.0f, anisotropy = 1.0f;
    VkSampler handle = VK_NULL_HANDLE;

    void Drop() {
        if (handle != VK_NULL_HANDLE) {
            VkSampler h = handle;
            VkBackend::DeferDestroy([h]() { vkDestroySampler(VkBackend::Device(), h, NULL); });
            handle = VK_NULL_HANDLE;
        }
    }
};

std::unordered_map<GLuint, SamplerObj>& samplers = *new std::unordered_map<GLuint, SamplerObj>();

VkSamplerAddressMode AddressMode(GLint wrap) {
    switch (wrap) {
        case GL_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GL_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case GL_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkCompareOp CompareOp(GLint func) {
    switch (func) {
        case GL_NEVER: return VK_COMPARE_OP_NEVER;
        case GL_LESS: return VK_COMPARE_OP_LESS;
        case GL_EQUAL: return VK_COMPARE_OP_EQUAL;
        case GL_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GL_GREATER: return VK_COMPARE_OP_GREATER;
        case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case GL_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

uint32_t NewName() {
    return VkStub::NewId();
}

void ShutdownAll() {
    for (auto& kv : textures) {
        kv.second.DestroyViews();
    }
    textures.clear();
    buffers.clear();
    for (auto& kv : samplers) {
        kv.second.Drop();
    }
    samplers.clear();
    DestroyChunk(current_chunk);
    for (Chunk& c : busy_chunks) {
        DestroyChunk(c);
    }
    busy_chunks.clear();
    busy_bytes = 0;
    for (Chunk& c : free_chunks) {
        DestroyChunk(c);
    }
    free_chunks.clear();
    shut_down = true;
}

// (Registers the shutdown with the backend the first time a resource is made)
void Hook() {
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkBackend::OnShutdown(&ShutdownAll);
        VkBackend::OnFrameBegin(&RecycleChunks);  // (staging back to its keep after a load)
    }
}

}  // namespace

// ---- textures

void APIENTRY CreateTextures(GLenum target, GLsizei n, GLuint* out) {
    if (shut_down) {
        return;
    }
    Hook();
    for (GLsizei i = 0; i < n; ++i) {
        out[i] = NewName();
        textures[out[i]].target = target;
    }
}

void APIENTRY GenTextures(GLsizei n, GLuint* out) {
    if (shut_down) {
        return;
    }
    CreateTextures(0, n, out);
}

void APIENTRY DeleteTextures(GLsizei n, const GLuint* names) {
    if (shut_down) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = textures.find(names[i]);
        if (it != textures.end()) {
            it->second.DestroyViews();
            textures.erase(it);  // (the storage goes with its last view)
        }
    }
}

GLboolean APIENTRY IsTexture(GLuint texture) {
    if (shut_down) {
        return GL_FALSE;
    }
    return textures.count(texture) ? GL_TRUE : GL_FALSE;
}

void APIENTRY TextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    if (shut_down) {
        return;
    }
    MakeStorage(texture, GL_TEXTURE_2D, levels, internalformat, width, height, 1, 1);
}

void APIENTRY TextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    if (shut_down) {
        return;
    }
    MakeStorage(texture, GL_TEXTURE_2D_ARRAY, levels, internalformat, width, height, depth, 1);
}

void APIENTRY TextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height,
                                          GLboolean fixedsamplelocations) {
    if (shut_down) {
        return;
    }
    (void)fixedsamplelocations;
    MakeStorage(texture, GL_TEXTURE_2D_MULTISAMPLE, 1, internalformat, width, height, 1, samples);
}

void APIENTRY TextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,
                                GLenum type, const void* pixels) {
    if (shut_down) {
        return;
    }
    SubImage(texture, level, xoffset, yoffset, 0, width, height, 1, format, type, pixels);
}

void APIENTRY TextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                GLsizei depth, GLenum format, GLenum type, const void* pixels) {
    if (shut_down) {
        return;
    }
    SubImage(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}

void APIENTRY CompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                                          GLenum format, GLsizei imageSize, const void* data) {
    if (shut_down) {
        return;
    }
    (void)format;
    CompressedSubImage(texture, level, xoffset, yoffset, 0, width, height, 1, imageSize, data);
}

void APIENTRY CompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width,
                                          GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void* data) {
    if (shut_down) {
        return;
    }
    (void)format;
    CompressedSubImage(texture, level, xoffset, yoffset, zoffset, width, height, depth, imageSize, data);
}

void APIENTRY GenerateTextureMipmap(GLuint texture) {
    if (shut_down) {
        return;
    }
    Texture* t = FindTexture(texture, "GenerateTextureMipmap");
    Storage* s = StorageOf(t, "GenerateTextureMipmap");
    if (s == NULL || t->num_levels < 2) {
        return;
    }
    if (s->fmt.kind == VkFormats::kCompressed || s->samples > 1) {
        LogOnce("mipmaps asked for a compressed or multisampled texture");
        return;
    }
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(VkBackend::PhysicalDevice(), s->fmt.format, &fp);
    const VkFilter filter = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) && s->fmt.kind == VkFormats::kColor
                                ? VK_FILTER_LINEAR
                                : VK_FILTER_NEAREST;
    VkCommandBuffer cmd = Cmd();
    const uint32_t b = t->base_level, layer = t->base_layer, layers = t->num_layers;
    for (uint32_t i = 1; i < t->num_levels; ++i) {
        const uint32_t src = b + i - 1, dst = b + i;
        Transition(*s, src, 1, layer, layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Transition(*s, dst, 1, layer, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blit = {};
        blit.srcSubresource = {s->aspect, src, layer, layers};
        blit.srcOffsets[1] = {(int32_t)s->MipWidth(src), (int32_t)s->MipHeight(src), (int32_t)s->MipDepth(src)};
        blit.dstSubresource = {s->aspect, dst, layer, layers};
        blit.dstOffsets[1] = {(int32_t)s->MipWidth(dst), (int32_t)s->MipHeight(dst), (int32_t)s->MipDepth(dst)};
        vkCmdBlitImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);
        Transition(*s, src, 1, layer, layers, s->rest);
    }
    Transition(*s, b + t->num_levels - 1, 1, layer, layers, s->rest);
}

void APIENTRY TextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels,
                          GLuint minlayer, GLuint numlayers) {
    if (shut_down) {
        return;
    }
    Texture* t = FindTexture(texture, "TextureView");
    Texture* o = FindTexture(origtexture, "TextureView (original)");
    if (t == NULL || o == NULL || !o->storage) {
        return;
    }
    t->DestroyViews();
    t->target = target;
    t->storage = o->storage;
    t->base_level = o->base_level + minlevel;
    t->num_levels = numlevels;
    t->base_layer = o->base_layer + minlayer;
    t->num_layers = numlayers;
    t->view_format = internalformat;
    if (internalformat != o->storage->gl_format && VkFormats::ForGL(internalformat).format != o->storage->fmt.format) {
        LogOnce("a texture view in another format (not supported)");
    }
}

void APIENTRY TextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) {
    if (shut_down) {
        return;
    }
    Texture* t = FindTexture(texture, "TextureBuffer");
    if (t == NULL) {
        return;
    }
    t->DestroyViews();
    t->target = GL_TEXTURE_BUFFER;
    t->storage.reset();
    t->buffer = buffer;
    t->buffer_format = internalformat;
}

void APIENTRY GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint* params) {
    if (shut_down) {
        return;
    }
    *params = 0;
    Texture* t = FindTexture(texture, "GetTextureLevelParameteriv");
    if (t == NULL || !t->storage) {
        return;
    }
    const Storage& s = *t->storage;
    const uint32_t lvl = t->base_level + (uint32_t)level;
    switch (pname) {
        case GL_TEXTURE_WIDTH: *params = (GLint)s.MipWidth(lvl); break;
        case GL_TEXTURE_HEIGHT: *params = (GLint)s.MipHeight(lvl); break;
        case GL_TEXTURE_DEPTH: *params = (GLint)(s.type == VK_IMAGE_TYPE_3D ? s.MipDepth(lvl) : t->num_layers); break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = (GLint)(t->view_format ? t->view_format : s.gl_format); break;
        case GL_TEXTURE_COMPRESSED: *params = s.fmt.kind == VkFormats::kCompressed ? 1 : 0; break;
        case GL_TEXTURE_COMPRESSED_IMAGE_SIZE: *params = (GLint)VkFormats::Size(s.fmt, s.MipWidth(lvl), s.MipHeight(lvl), s.MipDepth(lvl)); break;
        case GL_TEXTURE_SAMPLES: *params = (GLint)s.samples; break;
        default: LogOnce("a texture level parameter not answered"); break;
    }
}

void APIENTRY GetTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                 GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, void* pixels) {
    if (shut_down) {
        return;
    }
    memset(pixels, 0, bufSize);
    Texture* t = FindTexture(texture, "GetTextureSubImage");
    Storage* s = StorageOf(t, "GetTextureSubImage");
    if (s == NULL || s->fmt.kind == VkFormats::kCompressed) {
        return;
    }
    const VkFormats::Info fmt = s->fmt;
    ReadBack(t, *s, level, xoffset, yoffset, zoffset, width, height, depth, [&](const uint8_t* texels, size_t) {
        VkFormats::ConvertBack(fmt, texels, width, height, depth, format, type, pixels, bufSize);
    });
}

void APIENTRY GetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels) {
    if (shut_down) {
        return;
    }
    Texture* t = FindTexture(texture, "GetTextureImage");
    if (t == NULL || !t->storage) {
        memset(pixels, 0, bufSize);
        return;
    }
    const Storage& s = *t->storage;
    const uint32_t lvl = t->base_level + (uint32_t)level;
    const GLsizei d = s.type == VK_IMAGE_TYPE_3D ? (GLsizei)s.MipDepth(lvl) : (GLsizei)t->num_layers;
    GetTextureSubImage(texture, level, 0, 0, 0, s.MipWidth(lvl), s.MipHeight(lvl), d, format, type, bufSize, pixels);
}

void APIENTRY GetCompressedTextureImage(GLuint texture, GLint level, GLsizei bufSize, void* pixels) {
    if (shut_down) {
        return;
    }
    memset(pixels, 0, bufSize);
    Texture* t = FindTexture(texture, "GetCompressedTextureImage");
    Storage* s = StorageOf(t, "GetCompressedTextureImage");
    if (s == NULL || s->fmt.kind != VkFormats::kCompressed) {
        return;
    }
    const uint32_t lvl = t->base_level + (uint32_t)level;
    const GLsizei d = s->type == VK_IMAGE_TYPE_3D ? (GLsizei)s->MipDepth(lvl) : (GLsizei)t->num_layers;
    ReadBack(t, *s, level, 0, 0, 0, s->MipWidth(lvl), s->MipHeight(lvl), d, [&](const uint8_t* texels, size_t bytes) {
        memcpy(pixels, texels, std::min<size_t>(bytes, (size_t)bufSize));
    });
}

// ---- buffers

void APIENTRY CreateBuffers(GLsizei n, GLuint* out) {
    if (shut_down) {
        return;
    }
    Hook();
    for (GLsizei i = 0; i < n; ++i) {
        out[i] = NewName();
        buffers[out[i]];
    }
}

void APIENTRY GenBuffers(GLsizei n, GLuint* out) {
    if (shut_down) {
        return;
    }
    CreateBuffers(n, out);
}

void APIENTRY DeleteBuffers(GLsizei n, const GLuint* names) {
    if (shut_down) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        buffers.erase(names[i]);  // (the memory goes once the GPU is done with it)
    }
}

GLboolean APIENTRY IsBuffer(GLuint buffer) {
    if (shut_down) {
        return GL_FALSE;
    }
    return buffers.count(buffer) ? GL_TRUE : GL_FALSE;
}

void APIENTRY NamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags) {
    if (shut_down) {
        return;
    }
    BufferObj* o = FindBuffer(buffer, "NamedBufferStorage");
    if (o == NULL) {
        return;
    }
    o->host = (flags & (GL_MAP_WRITE_BIT | GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_CLIENT_STORAGE_BIT)) != 0;
    o->immutable = true;
    o->backing = MakeBacking(size, o->host);
    Fill(*o, data, size);
}

void APIENTRY NamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage) {
    if (shut_down) {
        return;
    }
    BufferObj* o = FindBuffer(buffer, "NamedBufferData");
    if (o == NULL) {
        return;
    }
    if (o->immutable) {
        LogOnce("NamedBufferData on immutable storage");
        return;
    }
    // (new memory each time: GL's "orphaning"; the old goes once the GPU is done with it)
    o->host = usage == GL_STREAM_DRAW || usage == GL_DYNAMIC_DRAW || usage == GL_STREAM_COPY || usage == GL_DYNAMIC_COPY;
    o->backing = MakeBacking(size, o->host);
    Fill(*o, data, size);
}

void APIENTRY NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) {
    if (shut_down) {
        return;
    }
    BufferObj* o = FindBuffer(buffer, "NamedBufferSubData");
    if (o == NULL || !o->backing || size <= 0) {
        return;
    }
    if ((VkDeviceSize)(offset + size) > o->backing->size) {
        LogOnce("NamedBufferSubData past the end");
        return;
    }
    Staging st = Stage(size);
    memcpy(st.ptr, data, size);
    CopyToBuffer(*o->backing, offset, st, size);
}

void* APIENTRY MapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    if (shut_down) {
        return NULL;
    }
    BufferObj* o = FindBuffer(buffer, "MapNamedBufferRange");
    if (o == NULL || !o->backing || o->mapped) {
        return NULL;
    }
    if ((VkDeviceSize)(offset + length) > o->backing->size) {
        LogOnce("MapNamedBufferRange past the end");
        return NULL;
    }
    if (access & GL_MAP_READ_BIT) {
        LogOnce("a buffer mapped for reading (not supported)");
        return NULL;
    }
    o->mapped = true;
    o->map_offset = offset;
    o->map_length = length;
    o->staged = false;
    if ((access & GL_MAP_INVALIDATE_BUFFER_BIT) && !o->immutable) {
        o->backing = MakeBacking(o->backing->size, true);  // (orphaned: fresh memory, nothing reads it yet)
        o->host = true;
        return o->backing->mapped + offset;
    }
    if (o->backing->mapped != NULL && (access & (GL_MAP_PERSISTENT_BIT | GL_MAP_UNSYNCHRONIZED_BIT))) {
        return o->backing->mapped + offset;  // (the engine keeps the GPU's reads apart itself)
    }
    o->staged = true;  // (copied in at the unmap, in GL's order)
    o->staging = Stage(length);
    return o->staging.ptr;
}

GLboolean APIENTRY UnmapNamedBuffer(GLuint buffer) {
    if (shut_down) {
        return GL_FALSE;
    }
    BufferObj* o = FindBuffer(buffer, "UnmapNamedBuffer");
    if (o == NULL || !o->mapped) {
        return GL_FALSE;
    }
    if (o->staged) {
        CopyToBuffer(*o->backing, o->map_offset, o->staging, o->map_length);
    }
    o->mapped = false;
    o->staged = false;
    return GL_TRUE;
}

void APIENTRY GetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void* data) {
    if (shut_down) {
        return;
    }
    memset(data, 0, size);
    LogOnce("GetNamedBufferSubData (not supported yet)");
    (void)buffer;
    (void)offset;
}

// ---- samplers

void APIENTRY CreateSamplers(GLsizei n, GLuint* out) {
    if (shut_down) {
        return;
    }
    Hook();
    for (GLsizei i = 0; i < n; ++i) {
        out[i] = NewName();
        samplers[out[i]];
    }
}

void APIENTRY DeleteSamplers(GLsizei count, const GLuint* names) {
    if (shut_down) {
        return;
    }
    for (GLsizei i = 0; i < count; ++i) {
        auto it = samplers.find(names[i]);
        if (it != samplers.end()) {
            it->second.Drop();
            samplers.erase(it);
        }
    }
}

void APIENTRY SamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    if (shut_down) {
        return;
    }
    auto it = samplers.find(sampler);
    if (it == samplers.end()) {
        LogOnce("SamplerParameteri: no such sampler");
        return;
    }
    SamplerObj& o = it->second;
    o.Drop();  // (made again from the new state when next asked for)
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: o.min_filter = param; break;
        case GL_TEXTURE_MAG_FILTER: o.mag_filter = param; break;
        case GL_TEXTURE_WRAP_S: o.wrap_s = param; break;
        case GL_TEXTURE_WRAP_T: o.wrap_t = param; break;
        case GL_TEXTURE_WRAP_R: o.wrap_r = param; break;
        case GL_TEXTURE_COMPARE_MODE: o.compare_mode = param; break;
        case GL_TEXTURE_COMPARE_FUNC: o.compare_func = param; break;
        default: SamplerParameterf(sampler, pname, (GLfloat)param); break;
    }
}

void APIENTRY SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    if (shut_down) {
        return;
    }
    auto it = samplers.find(sampler);
    if (it == samplers.end()) {
        LogOnce("SamplerParameterf: no such sampler");
        return;
    }
    SamplerObj& o = it->second;
    o.Drop();
    switch (pname) {
        case GL_TEXTURE_MIN_LOD: o.min_lod = param; break;
        case GL_TEXTURE_MAX_LOD: o.max_lod = param; break;
        case GL_TEXTURE_LOD_BIAS: o.lod_bias = param; break;
        case GL_TEXTURE_MAX_ANISOTROPY: o.anisotropy = param; break;
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_COMPARE_MODE:
        case GL_TEXTURE_COMPARE_FUNC:
            SamplerParameteri(sampler, pname, (GLint)param);
            break;
        default: LogOnce("a sampler parameter not kept"); break;
    }
}

// ---- fences

GLsync APIENTRY FenceSync(GLenum condition, GLbitfield flags) {
    if (shut_down) {
        return NULL;
    }
    (void)condition;
    (void)flags;
    return (GLsync)(uintptr_t)VkBackend::FenceSerial();
}

GLenum APIENTRY ClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    if (shut_down) {
        return GL_WAIT_FAILED;
    }
    const uint64_t serial = (uint64_t)(uintptr_t)sync;
    if (serial <= VkBackend::CompletedSerial()) {
        return GL_ALREADY_SIGNALED;
    }
    if (timeout == 0 && !(flags & GL_SYNC_FLUSH_COMMANDS_BIT)) {
        return GL_TIMEOUT_EXPIRED;
    }
    return VkBackend::WaitSerial(serial, timeout) ? GL_CONDITION_SATISFIED : GL_TIMEOUT_EXPIRED;
}

void APIENTRY DeleteSync(GLsync sync) {
    if (shut_down) {
        return;
    }
    (void)sync;
}

// ---- limits

void APIENTRY GetIntegerv(GLenum pname, GLint* data) {
    if (shut_down) {
        return;
    }
    const VkPhysicalDeviceLimits& l = VkBackend::Properties().limits;
    switch (pname) {
        case GL_MAX_UNIFORM_BLOCK_SIZE: *data = (GLint)std::min<uint32_t>(l.maxUniformBufferRange, 0x7fffffff); break;
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: *data = (GLint)l.minUniformBufferOffsetAlignment; break;
        case GL_MAX_TEXTURE_BUFFER_SIZE: *data = (GLint)std::min<uint32_t>(l.maxTexelBufferElements, 0x7fffffff); break;
        case GL_MAX_TEXTURE_SIZE: *data = (GLint)l.maxImageDimension2D; break;
        case GL_MAX_3D_TEXTURE_SIZE: *data = (GLint)l.maxImageDimension3D; break;
        case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = (GLint)l.maxImageArrayLayers; break;
        case GL_MAX_VERTEX_ATTRIBS: *data = (GLint)std::min<uint32_t>(l.maxVertexInputAttributes, 32); break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        case GL_MAX_TEXTURE_IMAGE_UNITS:
            *data = (GLint)std::min<uint32_t>(l.maxPerStageDescriptorSampledImages, 64);  // (the units the engine binds)
            break;
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS: *data = (GLint)(l.maxUniformBufferRange / 4); break;
        case GL_MAX_VARYING_FLOATS: *data = (GLint)l.maxVertexOutputComponents; break;
        case GL_MAX_SAMPLES: {
            VkSampleCountFlags c = l.framebufferColorSampleCounts & l.framebufferDepthSampleCounts;
            GLint n = 1;
            while ((VkSampleCountFlags)(n * 2) & c) {
                n *= 2;
            }
            *data = n;
            break;
        }
        default:
            *data = 0;
            LogOnce("a GL limit not answered");
            break;
    }
}

void APIENTRY GetFloatv(GLenum pname, GLfloat* data) {
    if (shut_down) {
        return;
    }
    const VkPhysicalDeviceLimits& l = VkBackend::Properties().limits;
    switch (pname) {
        case GL_MAX_TEXTURE_MAX_ANISOTROPY: *data = l.maxSamplerAnisotropy; break;
        default:
            GLint i = 0;
            GetIntegerv(pname, &i);
            *data = (GLfloat)i;
            break;
    }
}

// ---- for the passes and the checks

Stats GetStats() {
    Stats st;
    std::set<const Storage*> counted;
    for (const auto& kv : textures) {
        if (kv.second.storage && kv.second.storage->image != VK_NULL_HANDLE && counted.insert(kv.second.storage.get()).second) {
            ++st.images;
            st.image_bytes += kv.second.storage->bytes;
        }
    }
    for (const auto& kv : buffers) {
        if (kv.second.backing) {
            ++st.buffers;
            st.buffer_bytes += kv.second.backing->size;
        }
    }
    st.samplers = samplers.size();
    st.staging_bytes = current_chunk.size + busy_bytes;
    for (const Chunk& c : free_chunks) {
        st.staging_bytes += c.size;
    }
    st.uploads = uploads;
    st.upload_bytes = upload_bytes;
    return st;
}

bool Image(GLuint texture, ImageInfo* out) {
    auto it = textures.find(texture);
    if (it == textures.end() || !it->second.storage || it->second.storage->image == VK_NULL_HANDLE) {
        return false;
    }
    Texture& t = it->second;
    Storage& s = *t.storage;
    if (t.view == VK_NULL_HANDLE) {
        VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = s.image;
        switch (TargetOf(&t)) {
            case GL_TEXTURE_2D_ARRAY: info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
            case GL_TEXTURE_CUBE_MAP: info.viewType = t.num_layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
            case GL_TEXTURE_CUBE_MAP_ARRAY: info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY; break;
            case GL_TEXTURE_3D: info.viewType = VK_IMAGE_VIEW_TYPE_3D; break;
            default: info.viewType = t.num_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; break;
        }
        info.format = s.fmt.format;
        info.subresourceRange = {s.aspect, t.base_level, t.num_levels, t.base_layer, t.num_layers};
        if (vkCreateImageView(VkBackend::Device(), &info, NULL, &t.view) != VK_SUCCESS) {
            t.view = VK_NULL_HANDLE;
            return false;
        }
    }
    out->image = s.image;
    out->view = t.view;
    out->format = s.fmt.format;
    out->layout = s.rest;
    out->width = s.MipWidth(t.base_level);
    out->height = s.MipHeight(t.base_level);
    out->levels = t.num_levels;
    out->layers = t.num_layers;
    out->samples = s.samples;
    return true;
}

VkBuffer Buffer(GLuint buffer, VkDeviceSize* size) {
    auto it = buffers.find(buffer);
    if (it == buffers.end() || !it->second.backing) {
        return VK_NULL_HANDLE;
    }
    if (size) {
        *size = it->second.backing->size;
    }
    return it->second.backing->buffer;
}

VkBufferView TexelBuffer(GLuint texture) {
    auto it = textures.find(texture);
    if (it == textures.end() || it->second.target != GL_TEXTURE_BUFFER) {
        return VK_NULL_HANDLE;
    }
    Texture& t = it->second;
    VkDeviceSize size = 0;
    VkBuffer b = Buffer(t.buffer, &size);
    if (b == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    if (t.buffer_view != VK_NULL_HANDLE && t.buffer_view_of == b) {
        return t.buffer_view;
    }
    t.DestroyViews();  // (the buffer got new memory since)
    VkFormats::Info fmt = VkFormats::ForGL(t.buffer_format);
    VkBufferViewCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    info.buffer = b;
    info.format = fmt.format;
    info.offset = 0;
    const VkDeviceSize max_bytes = (VkDeviceSize)VkBackend::Properties().limits.maxTexelBufferElements * std::max(1u, fmt.texel_bytes);
    info.range = std::min(size - size % std::max(1u, fmt.texel_bytes), max_bytes);
    if (fmt.format == VK_FORMAT_UNDEFINED || vkCreateBufferView(VkBackend::Device(), &info, NULL, &t.buffer_view) != VK_SUCCESS) {
        t.buffer_view = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    t.buffer_view_of = b;
    return t.buffer_view;
}

bool SamplerParams(GLuint sampler, GLint* compare_mode, GLint* compare_func, GLint* min_filter) {
    auto it = samplers.find(sampler);
    if (it == samplers.end()) {
        return false;
    }
    *compare_mode = it->second.compare_mode;
    *compare_func = it->second.compare_func;
    *min_filter = it->second.min_filter;
    return true;
}

VkSampler Sampler(GLuint sampler) {
    auto it = samplers.find(sampler);
    if (it == samplers.end()) {
        return VK_NULL_HANDLE;
    }
    SamplerObj& o = it->second;
    if (o.handle != VK_NULL_HANDLE) {
        return o.handle;
    }
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = o.mag_filter == GL_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    bool mip = true;
    switch (o.min_filter) {
        case GL_NEAREST: info.minFilter = VK_FILTER_NEAREST; mip = false; break;
        case GL_LINEAR: info.minFilter = VK_FILTER_LINEAR; mip = false; break;
        case GL_NEAREST_MIPMAP_NEAREST: info.minFilter = VK_FILTER_NEAREST; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
        case GL_LINEAR_MIPMAP_NEAREST: info.minFilter = VK_FILTER_LINEAR; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
        case GL_NEAREST_MIPMAP_LINEAR: info.minFilter = VK_FILTER_NEAREST; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; break;
        default: info.minFilter = VK_FILTER_LINEAR; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; break;
    }
    info.addressModeU = AddressMode(o.wrap_s);
    info.addressModeV = AddressMode(o.wrap_t);
    info.addressModeW = AddressMode(o.wrap_r);
    const VkPhysicalDeviceLimits& l = VkBackend::Properties().limits;
    info.mipLodBias = std::max(-l.maxSamplerLodBias, std::min(l.maxSamplerLodBias, o.lod_bias));
    info.minLod = std::max(0.0f, o.min_lod);
    // (GL's non-mipmap minification: the base level only, mag/min still told apart)
    info.maxLod = mip ? (o.max_lod >= 1000.0f ? VK_LOD_CLAMP_NONE : o.max_lod) : 0.25f;
    if (o.anisotropy > 1.0f && mip && VkBackend::Features().samplerAnisotropy) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = std::min(o.anisotropy, l.maxSamplerAnisotropy);
    }
    if (o.compare_mode == GL_COMPARE_REF_TO_TEXTURE) {
        info.compareEnable = VK_TRUE;
        info.compareOp = CompareOp(o.compare_func);
        static const int debug_compare = config["vk_debug_compare"].toNumber<int>();  // (diagnostics: 1 always, 2 never)
        if (debug_compare == 1) {
            info.compareOp = VK_COMPARE_OP_ALWAYS;
        } else if (debug_compare == 2) {
            info.compareOp = VK_COMPARE_OP_NEVER;
        }
    }
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(VkBackend::Device(), &info, NULL, &o.handle) != VK_SUCCESS) {
        o.handle = VK_NULL_HANDLE;
    }
    return o.handle;
}

// ---- for the passes

namespace {

bool AttachmentRange(Texture* t, Storage& s, int level, int layer, uint32_t* lvl, uint32_t* lay) {
    *lvl = t->base_level + (uint32_t)std::max(0, level);
    *lay = t->base_layer + (uint32_t)std::max(0, layer);  // (-1: the texture itself, layer 0)
    return *lvl < s.levels && *lay < s.layers;
}

VkImageLayout AttachmentLayout(const Storage& s) {
    return s.fmt.kind == VkFormats::kDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

}  // namespace

// A view of one level and layer, kept with its texture
VkImageView LevelView(Texture& t, Storage& s, uint32_t lvl, uint32_t lay) {
    const uint64_t key = ((uint64_t)lvl << 32) | lay;
    auto v = t.attachment_views.find(key);
    if (v != t.attachment_views.end()) {
        return v->second;
    }
    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = s.image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = s.fmt.format;
    info.subresourceRange = {s.aspect, lvl, 1, lay, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(VkBackend::Device(), &info, NULL, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    t.attachment_views[key] = view;
    return view;
}

bool BeginAttachment(GLuint texture, int level, int layer, Attachment* out, bool read_only) {
    auto it = textures.find(texture);
    if (it == textures.end() || !it->second.storage || it->second.storage->image == VK_NULL_HANDLE) {
        LogOnce("an attachment that isn't a texture with storage");
        return false;
    }
    Texture& t = it->second;
    Storage& s = *t.storage;
    uint32_t lvl, lay;
    if (!AttachmentRange(&t, s, level, layer, &lvl, &lay)) {
        LogOnce("an attachment level or layer outside its texture");
        return false;
    }
    VkImageView view = LevelView(t, s, lvl, lay);
    if (view == VK_NULL_HANDLE) {
        return false;
    }
    const VkImageLayout layout =
        read_only && s.fmt.kind == VkFormats::kDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : AttachmentLayout(s);
    pass_setup = true;
    Transition(s, lvl, 1, lay, 1, layout);
    pass_setup = false;
    out->view = view;
    out->format = s.fmt.format;
    out->width = s.MipWidth(lvl);
    out->height = s.MipHeight(lvl);
    out->samples = s.samples;
    out->depth = s.fmt.kind == VkFormats::kDepth;
    out->layout = layout;
    return true;
}

void EndAttachment(GLuint texture, int level, int layer) {
    auto it = textures.find(texture);
    if (it == textures.end() || !it->second.storage || it->second.storage->image == VK_NULL_HANDLE) {
        return;
    }
    Texture& t = it->second;
    Storage& s = *t.storage;
    uint32_t lvl, lay;
    if (AttachmentRange(&t, s, level, layer, &lvl, &lay)) {
        pass_setup = true;
        Transition(s, lvl, 1, lay, 1, s.rest);
        pass_setup = false;
    }
}

void Blit(GLuint src, int src_level, int src_layer, const int src_rect[4], GLuint dst, int dst_level, int dst_layer, const int dst_rect[4],
          bool depth, bool linear) {
    Texture* st = FindTexture(src, "Blit (source)");
    Texture* dt = FindTexture(dst, "Blit (destination)");
    Storage* ss = StorageOf(st, "Blit (source)");
    Storage* ds = StorageOf(dt, "Blit (destination)");
    if (ss == NULL || ds == NULL) {
        return;
    }
    uint32_t sl, sy, dl, dy;
    if (!AttachmentRange(st, *ss, src_level, src_layer, &sl, &sy) || !AttachmentRange(dt, *ds, dst_level, dst_layer, &dl, &dy)) {
        LogOnce("a blit outside its textures");
        return;
    }
    if (ss == ds && sl == dl && sy == dy) {
        LogOnce("a blit within one level of one texture (not supported)");
        return;
    }
    VkCommandBuffer cmd = Cmd();
    Transition(*ss, sl, 1, sy, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Transition(*ds, dl, 1, dy, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const bool same_size = (src_rect[2] - src_rect[0]) == (dst_rect[2] - dst_rect[0]) && (src_rect[3] - src_rect[1]) == (dst_rect[3] - dst_rect[1]);
    if (ss->samples > 1 && ds->samples == 1 && depth && same_size && src_rect[0] == dst_rect[0] && src_rect[1] == dst_rect[1]) {
        // (GL's blit of multisampled depth resolves it to one value per pixel.
        // Vulkan resolves depth only at the end of a pass: an empty one with its resolve)
        VkImageView sv = LevelView(*st, *ss, sl, sy), dv = LevelView(*dt, *ds, dl, dy);
        Transition(*ss, sl, 1, sy, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        Transition(*ds, dl, 1, dy, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = sv;
        a.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // (each pixel's nearest sample, reversed-Z's largest: on NVIDIA nearest to what its GL
        // gives at edges, and the image's closest of the four modes, stage 5.5)
        a.resolveMode = (VkBackend::DepthResolveModes() & VK_RESOLVE_MODE_MAX_BIT) ? VK_RESOLVE_MODE_MAX_BIT : VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
        a.resolveImageView = dv;
        a.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{src_rect[0], src_rect[1]}, {(uint32_t)(src_rect[2] - src_rect[0]), (uint32_t)(src_rect[3] - src_rect[1])}};
        ri.layerCount = 1;
        ri.pDepthAttachment = &a;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdEndRendering(cmd);
    } else if (ss->samples > 1 && ds->samples == 1) {
        // (GL's blit out of a multisampled target resolves it)
        if (depth || !same_size) {
            LogOnce("a multisample resolve of depth that moves or scales (not supported)");
        } else {
            VkImageResolve r = {};
            r.srcSubresource = {aspect, sl, sy, 1};
            r.srcOffset = {src_rect[0], src_rect[1], 0};
            r.dstSubresource = {aspect, dl, dy, 1};
            r.dstOffset = {dst_rect[0], dst_rect[1], 0};
            r.extent = {(uint32_t)(src_rect[2] - src_rect[0]), (uint32_t)(src_rect[3] - src_rect[1]), 1};
            vkCmdResolveImage(cmd, ss->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ds->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        }
    } else if (ss->samples != ds->samples) {
        LogOnce("a blit between sample counts");
    } else {
        VkImageBlit b = {};
        b.srcSubresource = {aspect, sl, sy, 1};
        b.srcOffsets[0] = {src_rect[0], src_rect[1], 0};
        b.srcOffsets[1] = {src_rect[2], src_rect[3], 1};
        b.dstSubresource = {aspect, dl, dy, 1};
        b.dstOffsets[0] = {dst_rect[0], dst_rect[1], 0};
        b.dstOffsets[1] = {dst_rect[2], dst_rect[3], 1};
        vkCmdBlitImage(cmd, ss->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ds->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                       (linear && !depth) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    }
    Transition(*ss, sl, 1, sy, 1, ss->rest);
    Transition(*ds, dl, 1, dy, 1, ds->rest);
}

void BlitToSwapchain(GLuint src, VkImage dst, uint32_t width, uint32_t height) {
    Texture* t = FindTexture(src, "BlitToSwapchain");
    Storage* s = StorageOf(t, "BlitToSwapchain");
    if (s == NULL) {
        return;
    }
    VkCommandBuffer cmd = Cmd();
    Transition(*s, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // GL's row 0 is the bottom of the window, the swapchain's the top: the one flip
    VkImageBlit b = {};
    b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    b.srcOffsets[0] = {0, 0, 0};
    b.srcOffsets[1] = {(int32_t)s->width, (int32_t)s->height, 1};
    b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    b.dstOffsets[0] = {0, (int32_t)height, 0};
    b.dstOffsets[1] = {(int32_t)width, 0, 1};
    vkCmdBlitImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                   (s->width == width && s->height == height) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    Transition(*s, 0, 1, 0, 1, s->rest);
}

}  // namespace vkgl
