//-----------------------------------------------------------------------------
//           Name: vk_draw.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_draw.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#include "vk_draw.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_compile.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Graphics/Vulkan/vk_programs.h>
#include <Graphics/Vulkan/vk_queries.h>
#include <Graphics/textures.h>
#include <Graphics/graphics.h>
#include <Internal/filesystem.h>
#include <Internal/modid.h>
#include <Compat/fileio.h>
#include <Logging/logdata.h>

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VkDraw {
namespace {

void LogOnce(const std::string& what) {
    static std::set<std::string> seen;
    if (seen.insert(what).second) {
        LOGE << "Vulkan draws: " << what << std::endl;
    }
}

// ---- what's being drawn into

Target target;
int draw_count = -1;
bool rendering = false;

struct Active {
    Target target;  // (the window resolved to its textures)
    vkgl::Attachment color[Target::kMaxColor];
    int num_color = 0;
    vkgl::Attachment depth;
    bool has_depth = false;
    bool depth_read_only = false;  // (shaders sample it: see Draw)
    uint32_t width = 0, height = 0, samples = 1;
};
Active active;

GLuint window_color = 0, window_depth = 0;
int window_size[2] = {0, 0};

// The window's textures, made again when the drawable's size changes
void EnsureWindow() {
    int w = 0, h = 0;
    VkBackend::WindowSize(&w, &h);
    if (window_color != 0 && w == window_size[0] && h == window_size[1]) {
        return;
    }
    if (window_color != 0) {
        vkgl::DeleteTextures(1, &window_color);
        vkgl::DeleteTextures(1, &window_depth);
        window_color = window_depth = 0;
    }
    window_size[0] = w;
    window_size[1] = h;
    if (w <= 0 || h <= 0) {
        return;
    }
    vkgl::CreateTextures(GL_TEXTURE_2D, 1, &window_color);
    vkgl::TextureStorage2D(window_color, 1, GL_RGBA8, w, h);
    vkgl::CreateTextures(GL_TEXTURE_2D, 1, &window_depth);
    vkgl::TextureStorage2D(window_depth, 1, GL_DEPTH_COMPONENT32F, w, h);
}

Target Resolve(const Target& t) {
    if (!t.IsWindow()) {
        return t;
    }
    EnsureWindow();
    Target r;
    r.num_color = 1;
    r.color[0].texture = window_color;
    r.depth.texture = window_depth;
    return r;
}

bool SameTarget(const Target& a, const Target& b) {
    if (a.num_color != b.num_color || !(a.depth == b.depth)) {
        return false;
    }
    for (int i = 0; i < a.num_color; ++i) {
        if (!(a.color[i] == b.color[i])) {
            return false;
        }
    }
    return true;
}

uint64_t passes = 0, draws = 0, skipped = 0, pipelines_made = 0;
VkPipeline bound_pipeline = VK_NULL_HANDLE;

void EndRendering() {
    if (!rendering) {
        return;
    }
    rendering = false;
    VkQueries::OnPassEnd();
    vkCmdEndRendering(VkBackend::Commands());
    for (int i = 0; i < active.num_color; ++i) {
        vkgl::EndAttachment(active.target.color[i].texture, active.target.color[i].level, active.target.color[i].layer);
    }
    if (active.has_depth) {
        vkgl::EndAttachment(active.target.depth.texture, active.target.depth.level, active.target.depth.layer);
    }
}

void ResetForShutdown();  // (below: the stand-ins and pipelines)

void Hook() {
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkBackend::SetBeforeTransfer(&EndRendering);
        VkBackend::OnShutdown(&ResetForShutdown);
    }
}

bool BeginRendering(bool depth_read_only = false) {
    if (rendering) {
        return true;
    }
    Hook();
    Active a;
    a.target = Resolve(target);
    uint32_t w = UINT32_MAX, h = UINT32_MAX, samples = 0;
    for (int i = 0; i < a.target.num_color; ++i) {
        const TargetAttachment& t = a.target.color[i];
        if (t.texture == 0 || !vkgl::BeginAttachment(t.texture, t.level, t.layer, &a.color[a.num_color])) {
            break;
        }
        const vkgl::Attachment& c = a.color[a.num_color++];
        w = std::min(w, c.width);
        h = std::min(h, c.height);
        samples = std::max(samples, c.samples);
    }
    if (a.num_color != a.target.num_color) {
        for (int i = 0; i < a.num_color; ++i) {
            vkgl::EndAttachment(a.target.color[i].texture, a.target.color[i].level, a.target.color[i].layer);
        }
        LogOnce("a render target whose colour attachment can't be drawn into");
        return false;
    }
    if (a.target.depth.texture != 0) {
        if (!vkgl::BeginAttachment(a.target.depth.texture, a.target.depth.level, a.target.depth.layer, &a.depth, depth_read_only)) {
            for (int i = 0; i < a.num_color; ++i) {
                vkgl::EndAttachment(a.target.color[i].texture, a.target.color[i].level, a.target.color[i].layer);
            }
            LogOnce("a render target whose depth attachment can't be drawn into");
            return false;
        }
        a.has_depth = true;
        a.depth_read_only = depth_read_only;
        w = std::min(w, a.depth.width);
        h = std::min(h, a.depth.height);
        samples = std::max(samples, a.depth.samples);
    }
    if (w == UINT32_MAX || w == 0 || h == 0) {
        return false;
    }
    a.width = w;
    a.height = h;
    a.samples = std::max(1u, samples);

    VkRenderingAttachmentInfo colors[Target::kMaxColor];
    for (int i = 0; i < a.num_color; ++i) {
        colors[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colors[i].imageView = a.color[i].view;
        colors[i].imageLayout = a.color[i].layout;
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = a.depth.view;
    depth.imageLayout = a.depth.layout;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = a.depth_read_only ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {w, h}};
    info.layerCount = 1;
    info.colorAttachmentCount = (uint32_t)a.num_color;
    info.pColorAttachments = colors;
    info.pDepthAttachment = a.has_depth ? &depth : NULL;
    vkCmdBeginRendering(VkBackend::Commands(), &info);
    active = a;
    rendering = true;
    bound_pipeline = VK_NULL_HANDLE;
    ++passes;
    VkQueries::OnPassBegin();
    return true;
}

// ---- vertex input and uniform blocks

VertexInput vertex_input;
bool vertex_input_set = false;
uint64_t vertex_input_hash = 0;

struct Ubo {
    GLuint buffer = 0;
    size_t offset = 0, size = 0;
};
Ubo ubos[8];

uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

// ---- stand-ins: what GL reads where nothing is bound

struct Dummies {
    bool made = false;
    GLuint tex2d = 0, cube = 0, array = 0, tex3d = 0, cube_array = 0, depth2d = 0;
    GLuint sampler = 0, compare_sampler = 0;
    GLuint texel_buffer = 0, texel_float = 0, texel_uint = 0;
    GLuint vertex = 0, ubo = 0;
};
Dummies dummy;

void MakeDummies() {
    if (dummy.made) {
        return;
    }
    dummy.made = true;
    const uint8_t black[4] = {0, 0, 0, 255};  // (GL's incomplete texture: 0, 0, 0, 1)
    vkgl::CreateTextures(GL_TEXTURE_2D, 1, &dummy.tex2d);
    vkgl::TextureStorage2D(dummy.tex2d, 1, GL_RGBA8, 1, 1);
    vkgl::TextureSubImage2D(dummy.tex2d, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
    vkgl::CreateTextures(GL_TEXTURE_CUBE_MAP, 1, &dummy.cube);
    vkgl::TextureStorage2D(dummy.cube, 1, GL_RGBA8, 1, 1);
    for (int face = 0; face < 6; ++face) {
        vkgl::TextureSubImage3D(dummy.cube, 0, 0, 0, face, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
    }
    vkgl::CreateTextures(GL_TEXTURE_2D_ARRAY, 1, &dummy.array);
    vkgl::TextureStorage3D(dummy.array, 1, GL_RGBA8, 1, 1, 1);
    vkgl::TextureSubImage3D(dummy.array, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
    vkgl::CreateTextures(GL_TEXTURE_3D, 1, &dummy.tex3d);
    vkgl::TextureStorage3D(dummy.tex3d, 1, GL_RGBA8, 1, 1, 1);
    vkgl::TextureSubImage3D(dummy.tex3d, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
    vkgl::CreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &dummy.cube_array);
    vkgl::TextureStorage3D(dummy.cube_array, 1, GL_RGBA8, 1, 1, 6);
    vkgl::CreateTextures(GL_TEXTURE_2D, 1, &dummy.depth2d);
    vkgl::TextureStorage2D(dummy.depth2d, 1, GL_DEPTH_COMPONENT32F, 1, 1);
    vkgl::CreateSamplers(1, &dummy.sampler);
    vkgl::SamplerParameteri(dummy.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    vkgl::SamplerParameteri(dummy.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    vkgl::CreateSamplers(1, &dummy.compare_sampler);
    vkgl::SamplerParameteri(dummy.compare_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    vkgl::SamplerParameteri(dummy.compare_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    vkgl::SamplerParameteri(dummy.compare_sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    const uint32_t zeros[4] = {0, 0, 0, 0};
    vkgl::CreateBuffers(1, &dummy.texel_buffer);
    vkgl::NamedBufferStorage(dummy.texel_buffer, sizeof(zeros), zeros, 0);
    vkgl::CreateTextures(GL_TEXTURE_BUFFER, 1, &dummy.texel_float);
    vkgl::TextureBuffer(dummy.texel_float, GL_RGBA32F, dummy.texel_buffer);
    vkgl::CreateTextures(GL_TEXTURE_BUFFER, 1, &dummy.texel_uint);
    vkgl::TextureBuffer(dummy.texel_uint, GL_R32UI, dummy.texel_buffer);
    const float vertex[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // (GL's current attribute)
    vkgl::CreateBuffers(1, &dummy.vertex);
    vkgl::NamedBufferStorage(dummy.vertex, sizeof(vertex), vertex, 0);
    std::vector<uint8_t> block(65536, 0);
    vkgl::CreateBuffers(1, &dummy.ubo);
    vkgl::NamedBufferStorage(dummy.ubo, (GLsizeiptr)block.size(), block.data(), 0);
}

// ---- GL's enums as Vulkan's

VkFormat AttribFormat(GLint size, GLenum type, bool normalized) {
    const int i = std::max(1, std::min(4, (int)size)) - 1;
    switch (type) {
        case GL_FLOAT: {
            const VkFormat f[] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
            return f[i];
        }
        case GL_HALF_FLOAT: {
            const VkFormat f[] = {VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT};
            return f[i];
        }
        case GL_UNSIGNED_BYTE: {
            const VkFormat n[] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
            const VkFormat s[] = {VK_FORMAT_R8_USCALED, VK_FORMAT_R8G8_USCALED, VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8A8_USCALED};
            return normalized ? n[i] : s[i];
        }
        case GL_BYTE: {
            const VkFormat n[] = {VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM};
            const VkFormat s[] = {VK_FORMAT_R8_SSCALED, VK_FORMAT_R8G8_SSCALED, VK_FORMAT_R8G8B8_SSCALED, VK_FORMAT_R8G8B8A8_SSCALED};
            return normalized ? n[i] : s[i];
        }
        case GL_UNSIGNED_SHORT: {
            const VkFormat n[] = {VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM};
            const VkFormat s[] = {VK_FORMAT_R16_USCALED, VK_FORMAT_R16G16_USCALED, VK_FORMAT_R16G16B16_USCALED, VK_FORMAT_R16G16B16A16_USCALED};
            return normalized ? n[i] : s[i];
        }
        case GL_SHORT: {
            const VkFormat n[] = {VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM};
            const VkFormat s[] = {VK_FORMAT_R16_SSCALED, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R16G16B16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED};
            return normalized ? n[i] : s[i];
        }
        default:
            LogOnce("a vertex attribute type not converted");
            return VK_FORMAT_R32_SFLOAT;
    }
}

VkBlendFactor BlendFactor(GLenum f) {
    switch (f) {
        case GL_ZERO: return VK_BLEND_FACTOR_ZERO;
        case GL_ONE: return VK_BLEND_FACTOR_ONE;
        case GL_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GL_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case GL_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkCompareOp CompareOp(GLenum f) {
    switch (f) {
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

VkPrimitiveTopology Topology(GLenum mode) {
    switch (mode) {
        case GL_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case GL_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case GL_LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case GL_LINE_LOOP:
            LogOnce("GL_LINE_LOOP drawn as a strip (its closing line missing)");
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case GL_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case GL_PATCHES: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// The list topology of a topology's class (the pipeline's; the draw sets its own)
VkPrimitiveTopology TopologyClass(VkPrimitiveTopology t) {
    switch (t) {
        case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
        case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// ---- pipelines

struct Key {
    uint64_t program;
    uint64_t vertex;
    uint32_t color_formats[Target::kMaxColor];
    uint32_t depth_format;
    uint32_t num_color;
    uint32_t samples;
    uint32_t topology;
    uint32_t polygon_mode;
    uint32_t blend;  // enable | src << 4 | dst << 12 | alpha src << 20 (factor indices)
    uint32_t blend_alpha_dst;
    uint32_t write_masks;  // 4 bits per attachment
    uint32_t alpha_to_coverage;
};

struct KeyHash {
    size_t operator()(const Key& k) const { return (size_t)Fnv(&k, sizeof(k)); }
};
struct KeyEqual {
    bool operator()(const Key& a, const Key& b) const { return memcmp(&a, &b, sizeof(Key)) == 0; }
};
std::unordered_map<Key, VkPipeline, KeyHash, KeyEqual>& pipelines = *new std::unordered_map<Key, VkPipeline, KeyHash, KeyEqual>();  // (never destroyed: see vk_gl.cpp)

// ---- pipelines made ahead (stage 5.8). Every pipeline made is recorded in the write
// directory (Data/VulkanShaders/pipelines.txt: its program by name and optional
// stages, its state, the vertex input; with MSAA on, the scene's sample count as "S",
// so a record serves 2x, 4x and 8x). Packages ship a list recorded in test runs
// (scenario vk_pipeline_seed) beside the exe. A level's load makes the recorded ones
// for its programs on worker threads (PrecreatePipelines), and the menus warm the rest
// up (WarmUpPrograms ...), so a first draw doesn't wait for one. The pipeline cache
// (pipelines.cache) keeps the driver's work between runs

VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
bool pipeline_cache_tried = false;
std::set<std::string>& records = *new std::set<std::string>();  // (never destroyed: see vk_gl.cpp)
bool records_loaded = false;
const size_t kMaxRecords = 20000;

std::string ShaderCacheDir() {
    return GetWritePath(CoreGameModID) + "Data/VulkanShaders/";
}

bool ReadAll(const std::string& path, std::vector<char>& out) {
    FILE* f = my_fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.insert(out.end(), buf, buf + n);
    }
    fclose(f);
    return true;
}

void EnsurePipelineCache() {
    if (pipeline_cache_tried) {
        return;
    }
    pipeline_cache_tried = true;
    std::vector<char> data;
    const VkPhysicalDeviceProperties& p = VkBackend::Properties();
    if (ReadAll(ShaderCacheDir() + "pipelines.cache", data) && data.size() >= 16 + VK_UUID_SIZE) {
        // (VkPipelineCacheHeaderVersionOne: this GPU's and driver's only)
        uint32_t header[4];
        memcpy(header, data.data(), sizeof(header));
        if (header[0] < 16 + VK_UUID_SIZE || header[1] != VK_PIPELINE_CACHE_HEADER_VERSION_ONE || header[2] != p.vendorID ||
            header[3] != p.deviceID || memcmp(data.data() + 16, p.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
            data.clear();
        }
    } else {
        data.clear();
    }
    VkPipelineCacheCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? NULL : data.data();
    if (vkCreatePipelineCache(VkBackend::Device(), &info, NULL, &pipeline_cache) != VK_SUCCESS) {
        info.initialDataSize = 0;
        info.pInitialData = NULL;
        data.clear();
        if (vkCreatePipelineCache(VkBackend::Device(), &info, NULL, &pipeline_cache) != VK_SUCCESS) {
            pipeline_cache = VK_NULL_HANDLE;
        }
    }
    LOGI << "Vulkan pipeline cache: " << (data.empty() ? std::string("new") : std::to_string(data.size() >> 10) + " KB from earlier runs") << std::endl;
}

void SavePipelineCache() {
    if (pipeline_cache == VK_NULL_HANDLE) {
        return;
    }
    size_t n = 0;
    if (vkGetPipelineCacheData(VkBackend::Device(), pipeline_cache, &n, NULL) != VK_SUCCESS || n == 0) {
        return;
    }
    std::vector<char> data(n);
    if (vkGetPipelineCacheData(VkBackend::Device(), pipeline_cache, &n, data.data()) != VK_SUCCESS) {
        return;
    }
    const std::string path = ShaderCacheDir() + "pipelines.cache";
    CreateParentDirs(path);
    FILE* f = my_fopen(path.c_str(), "wb");
    if (f) {
        fwrite(data.data(), 1, n, f);
        fclose(f);
    }
}

uint64_t VertexHash(const VertexInput& in) {
    uint64_t h = 1469598103934665603ull;
    for (const VertexAttrib& a : in.attribs) {
        // (field by field: the struct's padding bytes aren't set, and differed between
        // equal layouts)
        const uint32_t f[6] = {a.location, a.binding, (uint32_t)a.size, (uint32_t)a.type, a.normalized ? 1u : 0u, a.offset};
        h = Fnv(f, sizeof(f), h);
    }
    return Fnv(in.divisors, sizeof(in.divisors), h);
}

std::string RecordOf(const Key& key, const VkPrograms::Info& prog, const VertexInput& in, int fsaa) {
    std::ostringstream o;
    o << "2|" << (fsaa > 1 ? "M" : "1") << "|" << prog.flags << "|" << prog.name << "|";
    for (int i = 0; i < Target::kMaxColor; ++i) {
        o << (i ? "," : "") << key.color_formats[i];
    }
    o << "|" << key.depth_format << "|" << key.num_color << "|";
    if (fsaa > 1 && key.samples == (uint32_t)fsaa) {
        o << "S";
    } else {
        o << key.samples;
    }
    o << "|" << key.topology << "|" << key.polygon_mode << "|"
      << key.blend << "|" << key.blend_alpha_dst << "|" << key.write_masks << "|" << key.alpha_to_coverage << "|";
    for (size_t i = 0; i < in.attribs.size(); ++i) {
        const VertexAttrib& a = in.attribs[i];
        o << (i ? ";" : "") << a.location << ":" << a.binding << ":" << a.size << ":" << a.type << ":" << (a.normalized ? 1 : 0) << ":" << a.offset;
    }
    o << "|";
    for (int b = 0; b < VertexInput::kMaxBindings; ++b) {
        o << (b ? "," : "") << in.divisors[b];
    }
    return o.str();
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            return out;
        }
        start = at + 1;
    }
}

uint32_t U32(const std::string& s) {
    return (uint32_t)strtoul(s.c_str(), NULL, 10);
}

// A record back to its key (program and vertex hash still to fill), vertex input,
// program name and optional stages, for MSAA setting fsaa. False: not one for this
// setting (MSAA off and on draw with other programs), or not one this build reads
bool ParseRecord(const std::string& r, int fsaa, std::string* name, uint32_t* flags, Key* key, VertexInput* in) {
    const std::vector<std::string> all = Split(r, '|');
    if (all.size() != 16 || all[0] != "2" || all[1] != (fsaa > 1 ? "M" : "1")) {
        return false;
    }
    *flags = U32(all[2]);
    const std::vector<std::string> f(all.begin() + 1, all.end());  // (the fields as before the flags)
    memset(key, 0, sizeof(*key));
    *name = f[2];
    const std::vector<std::string> formats = Split(f[3], ',');
    if (formats.size() != (size_t)Target::kMaxColor) {
        return false;
    }
    for (int i = 0; i < Target::kMaxColor; ++i) {
        key->color_formats[i] = U32(formats[i]);
    }
    key->depth_format = U32(f[4]);
    key->num_color = U32(f[5]);
    key->samples = f[6] == "S" ? (uint32_t)fsaa : U32(f[6]);
    key->topology = U32(f[7]);
    key->polygon_mode = U32(f[8]);
    key->blend = U32(f[9]);
    key->blend_alpha_dst = U32(f[10]);
    key->write_masks = U32(f[11]);
    key->alpha_to_coverage = U32(f[12]);
    if (key->num_color > (uint32_t)Target::kMaxColor) {
        return false;
    }
    in->attribs.clear();
    if (!f[13].empty()) {
        for (const std::string& a : Split(f[13], ';')) {
            const std::vector<std::string> v = Split(a, ':');
            if (v.size() != 6) {
                return false;
            }
            VertexAttrib va;
            va.location = U32(v[0]);
            va.binding = U32(v[1]);
            va.size = (GLint)U32(v[2]);
            va.type = (GLenum)U32(v[3]);
            va.normalized = U32(v[4]) != 0;
            va.offset = U32(v[5]);
            in->attribs.push_back(va);
        }
    }
    const std::vector<std::string> divisors = Split(f[14], ',');
    if (divisors.size() != (size_t)VertexInput::kMaxBindings) {
        return false;
    }
    for (int b = 0; b < VertexInput::kMaxBindings; ++b) {
        in->divisors[b] = U32(divisors[b]);
    }
    return true;
}

void LoadRecordFile(const std::string& path) {
    std::vector<char> data;
    if (!ReadAll(path, data)) {
        return;
    }
    std::string line;
    for (char c : data) {
        if (c == '\n') {
            if (!line.empty()) {
                records.insert(line);
            }
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
}

// The shipped list and this PC's (a record in either isn't written again)
void LoadRecords() {
    if (records_loaded) {
        return;
    }
    records_loaded = true;
    const std::string shipped = VkCompile::ShippedDir();
    if (!shipped.empty()) {
        LoadRecordFile(shipped + "pipelines.txt");
    }
    LoadRecordFile(ShaderCacheDir() + "pipelines.txt");
}

void Record(const Key& key, const VkPrograms::Info& prog, const VertexInput& in) {
    LoadRecords();
    if (records.size() >= kMaxRecords) {
        LogOnce("more pipelines than are recorded (the rest aren't made ahead)");
        return;
    }
    const std::string line = RecordOf(key, prog, in, Graphics::Instance()->config_.FSAA_samples());
    if (!records.insert(line).second) {
        return;
    }
    const std::string path = ShaderCacheDir() + "pipelines.txt";
    CreateParentDirs(path);
    FILE* f = my_fopen(path.c_str(), "ab");
    if (f) {
        fwrite(line.c_str(), 1, line.size(), f);
        fputc('\n', f);
        fclose(f);
    }
}

void DestroyPipelines() {
    for (auto& kv : pipelines) {
        VkPipeline p = kv.second;
        VkBackend::DeferDestroy([p]() { vkDestroyPipeline(VkBackend::Device(), p, NULL); });
    }
    pipelines.clear();
}

// (thread-safe: pipelines are made ahead on worker threads; the caller logs and counts)
VkPipeline MakePipeline(const Key& key, const VkPrograms::Info& prog, VkPrimitiveTopology topology, const VertexInput& vin) {
    // Vertex input: each program input from its attribute, or the stand-in binding
    const uint32_t kDummyBinding = VertexInput::kMaxBindings;
    std::vector<VkVertexInputAttributeDescription> attrs;
    std::vector<VkVertexInputBindingDescription> bindings;
    uint32_t used = 0;
    for (const VkPrograms::Input& in : prog.inputs) {
        VkVertexInputAttributeDescription a = {};
        a.location = in.location;
        const VertexAttrib* found = NULL;
        for (const VertexAttrib& va : vin.attribs) {
            if (va.location == in.location) {
                found = &va;
                break;
            }
        }
        if (found != NULL && found->binding < (uint32_t)VertexInput::kMaxBindings) {
            a.binding = found->binding;
            a.format = AttribFormat(found->size, found->type, found->normalized);
            a.offset = found->offset;
        } else {
            a.binding = kDummyBinding;
            a.format = in.format != VK_FORMAT_UNDEFINED ? in.format : VK_FORMAT_R32G32B32A32_SFLOAT;
            a.offset = 0;
        }
        used |= 1u << a.binding;
        attrs.push_back(a);
    }
    for (uint32_t b = 0; b <= kDummyBinding; ++b) {
        if (used & (1u << b)) {
            VkVertexInputBindingDescription d = {};
            d.binding = b;
            d.stride = 0;  // (dynamic)
            d.inputRate = (b < kDummyBinding && vin.divisors[b] != 0) ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            bindings.push_back(d);
        }
    }
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = (uint32_t)bindings.size();
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = TopologyClass(topology);
    VkPipelineTessellationStateCreateInfo ts = {VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    ts.patchControlPoints = 3;  // (glPatchParameteri(GL_PATCH_VERTICES, 3))
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = (VkPolygonMode)key.polygon_mode;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = (VkSampleCountFlagBits)key.samples;
    ms.alphaToCoverageEnable = key.alpha_to_coverage ? VK_TRUE : VK_FALSE;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blends[Target::kMaxColor] = {};
    for (uint32_t i = 0; i < key.num_color; ++i) {
        // (GL blends every draw buffer alike)
        blends[i].blendEnable = (key.blend & 1u) ? VK_TRUE : VK_FALSE;
        blends[i].srcColorBlendFactor = (VkBlendFactor)((key.blend >> 4) & 0xff);
        blends[i].dstColorBlendFactor = (VkBlendFactor)((key.blend >> 12) & 0xff);
        blends[i].colorBlendOp = VK_BLEND_OP_ADD;
        blends[i].srcAlphaBlendFactor = (VkBlendFactor)((key.blend >> 20) & 0xff);
        blends[i].dstAlphaBlendFactor = (VkBlendFactor)key.blend_alpha_dst;
        blends[i].alphaBlendOp = VK_BLEND_OP_ADD;
        blends[i].colorWriteMask = (key.write_masks >> (i * 4)) & 0xF;
    }
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = key.num_color;
    cb.pAttachments = blends;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                  VK_DYNAMIC_STATE_SCISSOR,
                                  VK_DYNAMIC_STATE_LINE_WIDTH,
                                  VK_DYNAMIC_STATE_DEPTH_BIAS,
                                  VK_DYNAMIC_STATE_CULL_MODE,
                                  VK_DYNAMIC_STATE_FRONT_FACE,
                                  VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                                  VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
                                  VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                  VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                  VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
                                  VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
                                  VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = (uint32_t)(sizeof(dyn) / sizeof(dyn[0]));
    dy.pDynamicStates = dyn;
    VkFormat colors[Target::kMaxColor];
    for (uint32_t i = 0; i < key.num_color; ++i) {
        colors[i] = (VkFormat)key.color_formats[i];
    }
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = key.num_color;
    ri.pColorAttachmentFormats = colors;
    ri.depthAttachmentFormat = (VkFormat)key.depth_format;

    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &ri;
    info.stageCount = (uint32_t)prog.stages.size();
    info.pStages = prog.stages.data();
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pTessellationState = prog.tessellation ? &ts : NULL;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = key.depth_format != VK_FORMAT_UNDEFINED ? &ds : NULL;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dy;
    info.layout = prog.layout;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(VkBackend::Device(), pipeline_cache, 1, &info, NULL, &p) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return p;
}

// ---- the menus' warm-up

struct WarmJob {
    Key key;
    const VkPrograms::Info* prog;
    VertexInput in;
};
std::vector<WarmJob>& warm_jobs = *new std::vector<WarmJob>();
std::vector<std::pair<Key, VkPipeline> >& warm_done = *new std::vector<std::pair<Key, VkPipeline> >();  // (made, not yet in the table)
std::mutex& warm_lock = *new std::mutex();
std::vector<std::thread>& warm_threads = *new std::vector<std::thread>();
std::atomic<size_t> warm_next(0);
std::atomic<bool> warm_stop(false);
std::atomic<int> warm_running(0);
bool warm_complete = false;

void WarmWorker() {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);  // (the menus stay smooth)
    for (size_t i = warm_next++; i < warm_jobs.size() && !warm_stop; i = warm_next++) {
        const WarmJob& j = warm_jobs[i];
        VkPipeline p = MakePipeline(j.key, *j.prog, (VkPrimitiveTopology)j.key.topology, j.in);
        if (p != VK_NULL_HANDLE) {
            std::lock_guard<std::mutex> lock(warm_lock);
            warm_done.push_back(std::make_pair(j.key, p));
        }
    }
    --warm_running;
}

void Drain() {
    std::vector<std::pair<Key, VkPipeline> > done;
    {
        std::lock_guard<std::mutex> lock(warm_lock);
        done.swap(warm_done);
    }
    for (const auto& d : done) {
        if (pipelines.count(d.first) == 0) {
            pipelines[d.first] = d.second;
            ++pipelines_made;
        } else {
            VkPipeline p = d.second;  // (made meanwhile by a draw)
            VkBackend::DeferDestroy([p]() { vkDestroyPipeline(VkBackend::Device(), p, NULL); });
        }
    }
}

void StopWarm() {
    warm_stop = true;
    for (std::thread& t : warm_threads) {
        t.join();
    }
    warm_threads.clear();
    warm_stop = false;
    Drain();
    if (warm_next < warm_jobs.size()) {
        warm_complete = false;  // (stopped part way: StartWarmUp goes on later)
    }
    warm_jobs.clear();
}

void ResetForShutdown() {
    StopWarm();
    rendering = false;
    window_color = window_depth = 0;  // (vk_gl destroys them, and the stand-ins)
    window_size[0] = window_size[1] = 0;
    dummy = Dummies();
    DestroyPipelines();
    SavePipelineCache();
    if (pipeline_cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(VkBackend::Device(), pipeline_cache, NULL);
        pipeline_cache = VK_NULL_HANDLE;
    }
}

}  // namespace

// ---- targets

void SetTarget(const Target& t) {
    Hook();
    if (rendering && !SameTarget(Resolve(t), active.target)) {
        EndRendering();
    }
    target = t;
}

void SetDrawCount(int count) {
    draw_count = count;
}

void Clear(bool color, bool depth, const float rgba[4], bool scissor_test, const int scissor[4]) {
    if (rendering && depth && active.depth_read_only) {
        EndRendering();
    }
    if (!BeginRendering()) {
        return;
    }
    VkClearAttachment atts[Target::kMaxColor + 1];
    uint32_t n = 0;
    const int written = draw_count < 0 ? active.num_color : std::min(draw_count, active.num_color);
    if (color) {
        for (int i = 0; i < written; ++i) {
            atts[n] = {};
            atts[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            atts[n].colorAttachment = (uint32_t)i;
            memcpy(atts[n].clearValue.color.float32, rgba, sizeof(float) * 4);
            ++n;
        }
    }
    if (depth && active.has_depth) {
        atts[n] = {};
        atts[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        atts[n].clearValue.depthStencil.depth = 0.0f;  // (reversed-Z: GL's glClearDepth(0))
        ++n;
    }
    int x0 = 0, y0 = 0, x1 = (int)active.width, y1 = (int)active.height;
    if (scissor_test) {
        x0 = std::max(x0, scissor[0]);
        y0 = std::max(y0, scissor[1]);
        x1 = std::min(x1, scissor[0] + scissor[2]);
        y1 = std::min(y1, scissor[1] + scissor[3]);
    }
    if (n == 0 || x1 <= x0 || y1 <= y0) {
        return;
    }
    VkClearRect rect = {{{x0, y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}}, 0, 1};
    vkCmdClearAttachments(VkBackend::Commands(), n, atts, 1, &rect);
}

// ---- vertex input and blocks

void SetVertexInput(const VertexInput& input) {
    vertex_input = input;
    vertex_input_set = true;
    vertex_input_hash = VertexHash(input);
}

void ClearVertexInput() {
    vertex_input_set = false;
}

void SetUniformBuffer(uint32_t binding, GLuint buffer, size_t offset, size_t size) {
    if (binding < sizeof(ubos) / sizeof(ubos[0])) {
        ubos[binding].buffer = buffer;
        ubos[binding].offset = offset;
        ubos[binding].size = size;
    }
}

// ---- draws

void Draw(const State& st, GLuint program, GLenum mode, uint32_t first, uint32_t count, bool indexed, GLenum index_type,
          size_t index_offset, uint32_t instances) {
    const VkPrograms::Info* prog = VkPrograms::Get(program);
    if (prog == NULL || !vertex_input_set || count == 0 || instances == 0) {
        ++skipped;
        return;
    }
    if (indexed && index_type == GL_UNSIGNED_BYTE) {
        LogOnce("8-bit indices (not supported)");
        ++skipped;
        return;
    }
    MakeDummies();
    // Descriptors: the program's textures (what GL has at each unit) and blocks
    Textures* textures = Textures::Instance();
    const VkDeviceSize max_range = VkBackend::Properties().limits.maxUniformBufferRange;
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkBufferView> views;
    writes.reserve(prog->bindings.size());
    images.reserve(prog->bindings.size());
    buffers.reserve(prog->bindings.size());
    views.reserve(prog->bindings.size());
    // (a texture read here that's also the target's depth attachment: GL allows it while
    // depth isn't written, as the transparent pass reads the scene's depth with MSAA off.
    // The pass's depth goes read-only for it)
    VkImage depth_image = VK_NULL_HANDLE;
    {
        const TargetAttachment d = rendering ? active.target.depth : Resolve(target).depth;
        vkgl::ImageInfo di;
        if (d.texture != 0 && vkgl::Image(d.texture, &di)) {
            depth_image = di.image;
        }
    }
    bool reads_depth = false;
    for (const VkPrograms::Binding& b : prog->bindings) {
        VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = b.binding;
        w.descriptorCount = 1;
        w.descriptorType = b.type;
        if (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            const Ubo& u = ubos[b.unit < 8 ? b.unit : 0];
            VkDeviceSize size = 0;
            VkBuffer vb = u.buffer != 0 ? vkgl::Buffer(u.buffer, &size) : VK_NULL_HANDLE;
            VkDescriptorBufferInfo bi = {};
            if (vb == VK_NULL_HANDLE || u.offset >= size) {
                bi.buffer = vkgl::Buffer(dummy.ubo, NULL);
                bi.range = std::min<VkDeviceSize>(std::max<VkDeviceSize>(b.block_size, 16), 65536);
            } else {
                bi.buffer = vb;
                bi.offset = u.offset;
                bi.range = std::min<VkDeviceSize>(std::min<VkDeviceSize>(std::max<VkDeviceSize>(u.size, b.block_size), size - u.offset), max_range);
            }
            buffers.push_back(bi);
            w.pBufferInfo = &buffers.back();
        } else if (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
            GLuint tex = 0, smp = 0;
            textures->UnitBinding((int)b.unit, Textures::kUnitBuffer, &tex, &smp);
            VkBufferView v = tex != 0 ? vkgl::TexelBuffer(tex) : VK_NULL_HANDLE;
            if (v == VK_NULL_HANDLE) {
                v = vkgl::TexelBuffer(b.integer ? dummy.texel_uint : dummy.texel_float);
            }
            views.push_back(v);
            w.pTexelBufferView = &views.back();
        } else {
            Textures::UnitKind kind = Textures::kUnit2D;
            GLuint stand_in_tex = b.shadow ? dummy.depth2d : dummy.tex2d;
            switch (b.view) {
                case VK_IMAGE_VIEW_TYPE_CUBE: kind = Textures::kUnitCube; stand_in_tex = dummy.cube; break;
                case VK_IMAGE_VIEW_TYPE_2D_ARRAY: kind = Textures::kUnit2DArray; stand_in_tex = dummy.array; break;
                case VK_IMAGE_VIEW_TYPE_3D: kind = Textures::kUnit3D; stand_in_tex = dummy.tex3d; break;
                case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY: kind = Textures::kUnitCubeArray; stand_in_tex = dummy.cube_array; break;
                default: kind = b.multisampled ? Textures::kUnit2DMultisample : Textures::kUnit2D; break;
            }
            GLuint tex = 0, smp = 0;
            textures->UnitBinding((int)b.unit, kind, &tex, &smp);
            vkgl::ImageInfo ii;
            bool have = tex != 0 && vkgl::Image(tex, &ii);
            if (have && b.shadow && ii.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
                have = false;  // (a colour texture where a depth comparison reads; depth read as a value is fine)
            }
            if (have && b.used && depth_image != VK_NULL_HANDLE && ii.image == depth_image) {
                reads_depth = true;
            }
            if (!have) {
                if (b.multisampled) {
                    LogOnce("a multisample sampler with nothing bound (no stand-in)");
                }
                static const bool debug_standins = getenv("OGMP_VK_DEBUG_STANDINS") != NULL;  // (diagnostics)
                if (debug_standins) {
                    static std::set<std::pair<uint64_t, uint32_t> > logged;
                    if (logged.insert(std::make_pair(prog->id, b.unit)).second) {
                        LOGI << "Vulkan stand-in: " << prog->name << " unit " << b.unit << " view " << (int)b.view << " texture " << tex << std::endl;
                    }
                }
                vkgl::Image(stand_in_tex, &ii);
                smp = 0;
            }
            VkSampler sampler = smp != 0 ? vkgl::Sampler(smp) : VK_NULL_HANDLE;
            if (sampler == VK_NULL_HANDLE) {
                sampler = vkgl::Sampler(b.shadow ? dummy.compare_sampler : dummy.sampler);
            }
            VkDescriptorImageInfo info = {sampler, ii.view, ii.layout};
            images.push_back(info);
            w.pImageInfo = &images.back();
        }
        writes.push_back(w);
    }
    const bool depth_write = st.depth_test && st.depth_write;
    if (rendering && active.has_depth && (reads_depth ? !active.depth_read_only : (active.depth_read_only && depth_write))) {
        EndRendering();
    }
    if (!BeginRendering(reads_depth)) {
        ++skipped;
        return;
    }
    if (reads_depth && depth_write) {
        LogOnce("a draw that samples the depth it writes (its depth writes dropped)");
    }
    const VkPrimitiveTopology topology = Topology(mode);

    Key key;
    memset(&key, 0, sizeof(key));
    key.program = prog->id;
    key.vertex = vertex_input_hash;
    key.num_color = (uint32_t)active.num_color;
    for (int i = 0; i < active.num_color; ++i) {
        key.color_formats[i] = (uint32_t)active.color[i].format;
    }
    key.depth_format = active.has_depth ? (uint32_t)active.depth.format : (uint32_t)VK_FORMAT_UNDEFINED;
    key.samples = active.samples;
    key.topology = (uint32_t)TopologyClass(topology);
    key.polygon_mode = st.wireframe && VkBackend::Features().fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    if (st.blend) {
        key.blend = 1u | ((uint32_t)BlendFactor(st.blend_src) << 4) | ((uint32_t)BlendFactor(st.blend_dst) << 12) |
                    ((uint32_t)BlendFactor(st.blend_alpha_src) << 20);
        key.blend_alpha_dst = (uint32_t)BlendFactor(st.blend_alpha_dst);
    }
    // (attachments past GL's draw buffers, or that the shader doesn't write: left as they are)
    const int written = std::min(draw_count < 0 ? active.num_color : std::min(draw_count, active.num_color), (int)prog->color_outputs);
    for (int i = 0; i < written; ++i) {
        key.write_masks |= (uint32_t)(st.color_mask & 0xF) << (i * 4);
    }
    key.alpha_to_coverage = (st.alpha_to_coverage && active.samples > 1) ? 1u : 0u;

    VkPipeline pipeline;
    auto found = pipelines.find(key);
    if (found != pipelines.end()) {
        pipeline = found->second;
    } else {
        EnsurePipelineCache();
        pipeline = MakePipeline(key, *prog, topology, vertex_input);
        if (pipeline != VK_NULL_HANDLE) {
            pipelines[key] = pipeline;
            ++pipelines_made;
            Record(key, *prog, vertex_input);
        } else {
            LogOnce("vkCreateGraphicsPipelines failed for " + prog->name);
        }
        for (uint32_t b = 0; b < (uint32_t)VertexInput::kMaxBindings; ++b) {
            if (vertex_input.divisors[b] > 1) {
                LogOnce("a vertex divisor above 1 (drawn as 1)");
            }
        }
    }
    if (pipeline == VK_NULL_HANDLE) {
        ++skipped;
        return;
    }
    VkCommandBuffer cmd = VkBackend::Commands();
    if (pipeline != bound_pipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        bound_pipeline = pipeline;
    }

    // Dynamic state, as GL has it
    VkViewport viewport = {(float)st.viewport[0], (float)st.viewport[1], (float)std::max(1, st.viewport[2]), (float)std::max(1, st.viewport[3]), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    int sx0 = 0, sy0 = 0, sx1 = (int)active.width, sy1 = (int)active.height;
    if (st.scissor_test) {
        sx0 = std::max(sx0, st.scissor[0]);
        sy0 = std::max(sy0, st.scissor[1]);
        sx1 = std::min(sx1, st.scissor[0] + st.scissor[2]);
        sy1 = std::min(sy1, st.scissor[1] + st.scissor[3]);
    }
    VkRect2D scissor = {{sx0, sy0}, {(uint32_t)std::max(0, sx1 - sx0), (uint32_t)std::max(0, sy1 - sy0)}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetLineWidth(cmd, VkBackend::Features().wideLines ? std::max(1.0f, st.line_width) : 1.0f);
    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);  // (GL's polygon offset values are never set: 0, 0)
    vkCmdSetCullMode(cmd, st.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
    // GL's counter-clockwise front, in GL's memory layout (no flipped viewport): clockwise here
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_CLOCKWISE);
    vkCmdSetPrimitiveTopology(cmd, topology);
    vkCmdSetDepthTestEnable(cmd, (st.depth_test && active.has_depth) ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, (depth_write && active.has_depth && !active.depth_read_only) ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, CompareOp(st.depth_func));
    vkCmdSetDepthBiasEnable(cmd, st.polygon_offset ? VK_TRUE : VK_FALSE);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);

    // Vertex buffers: every binding (the unused ones and the stand-in at the end)
    VkBuffer vbufs[VertexInput::kMaxBindings + 1];
    VkDeviceSize voffs[VertexInput::kMaxBindings + 1];
    VkDeviceSize vstrides[VertexInput::kMaxBindings + 1];
    VkBuffer stand_in = vkgl::Buffer(dummy.vertex, NULL);
    for (int b = 0; b < VertexInput::kMaxBindings; ++b) {
        VkBuffer vb = vertex_input.buffers[b] != 0 ? vkgl::Buffer(vertex_input.buffers[b], NULL) : VK_NULL_HANDLE;
        vbufs[b] = vb != VK_NULL_HANDLE ? vb : stand_in;
        voffs[b] = vb != VK_NULL_HANDLE ? vertex_input.offsets[b] : 0;
        vstrides[b] = vb != VK_NULL_HANDLE ? vertex_input.strides[b] : 0;
    }
    vbufs[VertexInput::kMaxBindings] = stand_in;
    voffs[VertexInput::kMaxBindings] = 0;
    vstrides[VertexInput::kMaxBindings] = 0;
    vkCmdBindVertexBuffers2(cmd, 0, VertexInput::kMaxBindings + 1, vbufs, voffs, NULL, vstrides);
    if (indexed) {
        VkBuffer ib = vkgl::Buffer(vertex_input.element_buffer, NULL);
        if (ib == VK_NULL_HANDLE) {
            LogOnce("an indexed draw with no index buffer");
            ++skipped;
            return;
        }
        vkCmdBindIndexBuffer(cmd, ib, 0, index_type == GL_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    }

    if (!writes.empty()) {
        vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->layout, 0, (uint32_t)writes.size(), writes.data());
    }

    if (indexed) {
        const uint32_t index_size = index_type == GL_UNSIGNED_SHORT ? 2 : 4;
        vkCmdDrawIndexed(cmd, count, instances, (uint32_t)(index_offset / index_size), 0, 0);
    } else {
        vkCmdDraw(cmd, count, instances, first, 0);
    }
    ++draws;
}

// ---- the window

void Present() {
    EndRendering();
    if (window_color == 0) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        VkBackend::Present(black);
        return;
    }
    const float unused[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLuint frame = window_color;
    VkBackend::Present(unused, [frame](VkImage image, uint32_t width, uint32_t height) { vkgl::BlitToSwapchain(frame, image, width, height); });
}

GLuint WindowColor() {
    EnsureWindow();
    return window_color;
}

bool ReadPixels(int x, int y, int w, int h, GLenum format, GLenum type, void* out, size_t out_size, int color) {
    EndRendering();
    Target t = Resolve(target);
    const TargetAttachment* a = NULL;
    if (format == GL_DEPTH_COMPONENT) {
        a = t.depth.texture != 0 ? &t.depth : NULL;
    } else if (color >= 0 && color < t.num_color) {
        a = &t.color[color];
    }
    if (a == NULL || a->texture == 0) {
        memset(out, 0, out_size);
        return false;
    }
    vkgl::GetTextureSubImage(a->texture, a->level, x, y, std::max(0, a->layer), w, h, 1, format, type, (GLsizei)out_size, out);
    return true;
}

void Blit(const Target& src, int src_color, const int src_rect[4], const Target& dst, int dst_color, const int dst_rect[4], bool depth,
          bool linear) {
    EndRendering();
    Target s = Resolve(src), d = Resolve(dst);
    const TargetAttachment* sa = depth ? &s.depth : (src_color < s.num_color ? &s.color[src_color] : NULL);
    const TargetAttachment* da = depth ? &d.depth : (dst_color < d.num_color ? &d.color[dst_color] : NULL);
    if (sa == NULL || da == NULL || sa->texture == 0 || da->texture == 0) {
        LogOnce("a blit with a missing attachment");
        return;
    }
    vkgl::Blit(sa->texture, sa->level, sa->layer, src_rect, da->texture, da->level, da->layer, dst_rect, depth, linear);
}

void PrecreatePipelines() {
    if (VkBackend::Device() == VK_NULL_HANDLE) {
        return;
    }
    StopWarm();  // (what the menus made so far is kept)
    const auto start = std::chrono::steady_clock::now();
    EnsurePipelineCache();
    LoadRecords();
    std::unordered_map<std::string, const VkPrograms::Info*> by_name;
    VkPrograms::ForEach([&](const VkPrograms::Info& info) { by_name[info.name] = &info; });
    const int fsaa = Graphics::Instance()->config_.FSAA_samples();
    struct Job {
        Key key;
        const VkPrograms::Info* prog;
        VertexInput in;
        VkPipeline result;
    };
    std::vector<Job> jobs;
    std::unordered_set<Key, KeyHash, KeyEqual> queued;
    for (const std::string& r : records) {
        Job j;
        std::string name;
        uint32_t flags = 0;
        if (!ParseRecord(r, fsaa, &name, &flags, &j.key, &j.in)) {
            continue;
        }
        auto it = by_name.find(name);
        if (it == by_name.end()) {
            continue;
        }
        j.prog = it->second;
        j.key.program = j.prog->id;
        j.key.vertex = VertexHash(j.in);
        j.result = VK_NULL_HANDLE;
        if (pipelines.count(j.key) != 0 || !queued.insert(j.key).second) {
            continue;
        }
        jobs.push_back(j);
    }
    if (jobs.empty()) {
        return;
    }
    unsigned threads = std::thread::hardware_concurrency();
    threads = std::max(1u, std::min(8u, threads > 1 ? threads - 1 : 1u));
    threads = (unsigned)std::min<size_t>(threads, jobs.size());
    std::atomic<size_t> next(0);
    auto work = [&]() {
        for (size_t i = next++; i < jobs.size(); i = next++) {
            jobs[i].result = MakePipeline(jobs[i].key, *jobs[i].prog, (VkPrimitiveTopology)jobs[i].key.topology, jobs[i].in);
        }
    };
    std::vector<std::thread> workers;
    for (unsigned t = 1; t < threads; ++t) {
        workers.emplace_back(work);
    }
    work();
    for (std::thread& t : workers) {
        t.join();
    }
    size_t made = 0;
    for (const Job& j : jobs) {
        if (j.result != VK_NULL_HANDLE) {
            pipelines[j.key] = j.result;
            ++pipelines_made;
            ++made;
        }
    }
    SavePipelineCache();
    LOGI << "Vulkan: " << made << " of " << jobs.size() << " recorded pipelines made ahead ("
         << (int)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms, " << threads
         << " threads)" << std::endl;
}

std::vector<std::pair<std::string, uint32_t> > WarmUpPrograms() {
    LoadRecords();
    const int fsaa = Graphics::Instance()->config_.FSAA_samples();
    std::set<std::pair<std::string, uint32_t> > seen;
    std::vector<std::pair<std::string, uint32_t> > out;
    for (const std::string& r : records) {
        std::string name;
        uint32_t flags = 0;
        Key key;
        VertexInput in;
        if (ParseRecord(r, fsaa, &name, &flags, &key, &in) && seen.insert(std::make_pair(name, flags)).second) {
            out.push_back(std::make_pair(name, flags));
        }
    }
    return out;
}

void StartWarmUp() {
    if (!warm_threads.empty() || warm_complete || VkBackend::Device() == VK_NULL_HANDLE) {
        return;
    }
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkPrograms::OnBeforeDestroy(&StopWarm);
    }
    EnsurePipelineCache();
    LoadRecords();
    std::unordered_map<std::string, const VkPrograms::Info*> by_name;
    VkPrograms::ForEach([&](const VkPrograms::Info& info) { by_name[info.name] = &info; });
    const int fsaa = Graphics::Instance()->config_.FSAA_samples();
    std::unordered_set<Key, KeyHash, KeyEqual> queued;
    warm_jobs.clear();
    for (const std::string& r : records) {
        WarmJob j;
        std::string name;
        uint32_t flags = 0;
        if (!ParseRecord(r, fsaa, &name, &flags, &j.key, &j.in)) {
            continue;
        }
        auto it = by_name.find(name);
        if (it == by_name.end()) {
            continue;
        }
        j.prog = it->second;
        j.key.program = j.prog->id;
        j.key.vertex = VertexHash(j.in);
        if (pipelines.count(j.key) == 0 && queued.insert(j.key).second) {
            warm_jobs.push_back(j);
        }
    }
    warm_complete = true;
    if (warm_jobs.empty()) {
        return;
    }
    unsigned threads = std::max(1u, std::min(3u, std::thread::hardware_concurrency() / 4));
    warm_next = 0;
    warm_stop = false;
    warm_running = (int)threads;
    for (unsigned t = 0; t < threads; ++t) {
        warm_threads.emplace_back(&WarmWorker);
    }
    LOGI << "Vulkan: warming up " << warm_jobs.size() << " recorded pipelines on " << threads << " threads" << std::endl;
}

bool WarmUpDone() {
    return warm_complete && warm_running == 0;
}

void DrainWarmUp() {
    Drain();
    if (!warm_threads.empty() && warm_running == 0) {
        StopWarm();  // (joins the finished threads)
        SavePipelineCache();
        LOGI << "Vulkan: pipeline warm-up done (" << pipelines.size() << " pipelines)" << std::endl;
    }
}

void StopWarmUp() {
    StopWarm();
}

Stats GetStats() {
    Stats s;
    s.draws = draws;
    s.pipelines = pipelines.size();
    s.passes = passes;
    s.skipped = skipped;
    return s;
}

}  // namespace VkDraw
