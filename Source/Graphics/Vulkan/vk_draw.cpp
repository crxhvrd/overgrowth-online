//-----------------------------------------------------------------------------
//           Name: vk_draw.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_draw.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#include "vk_draw.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Graphics/Vulkan/vk_programs.h>
#include <Graphics/Vulkan/vk_queries.h>
#include <Graphics/textures.h>
#include <Logging/logdata.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
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

void DestroyPipelines() {
    for (auto& kv : pipelines) {
        VkPipeline p = kv.second;
        VkBackend::DeferDestroy([p]() { vkDestroyPipeline(VkBackend::Device(), p, NULL); });
    }
    pipelines.clear();
}

VkPipeline MakePipeline(const Key& key, const VkPrograms::Info& prog, VkPrimitiveTopology topology) {
    // Vertex input: each program input from its attribute, or the stand-in binding
    const uint32_t kDummyBinding = VertexInput::kMaxBindings;
    std::vector<VkVertexInputAttributeDescription> attrs;
    std::vector<VkVertexInputBindingDescription> bindings;
    uint32_t used = 0;
    for (const VkPrograms::Input& in : prog.inputs) {
        VkVertexInputAttributeDescription a = {};
        a.location = in.location;
        const VertexAttrib* found = NULL;
        for (const VertexAttrib& va : vertex_input.attribs) {
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
            d.inputRate = (b < kDummyBinding && vertex_input.divisors[b] != 0) ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            if (b < kDummyBinding && vertex_input.divisors[b] > 1) {
                LogOnce("a vertex divisor above 1 (drawn as 1)");
            }
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
    if (vkCreateGraphicsPipelines(VkBackend::Device(), VK_NULL_HANDLE, 1, &info, NULL, &p) != VK_SUCCESS) {
        LogOnce("vkCreateGraphicsPipelines failed for " + prog.name);
        return VK_NULL_HANDLE;
    }
    ++pipelines_made;
    return p;
}

void ResetForShutdown() {
    rendering = false;
    window_color = window_depth = 0;  // (vk_gl destroys them, and the stand-ins)
    window_size[0] = window_size[1] = 0;
    dummy = Dummies();
    DestroyPipelines();
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
    uint64_t h = 1469598103934665603ull;
    for (const VertexAttrib& a : input.attribs) {
        h = Fnv(&a, sizeof(a), h);
    }
    h = Fnv(input.divisors, sizeof(input.divisors), h);
    vertex_input_hash = h;
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
        pipeline = MakePipeline(key, *prog, topology);
        if (pipeline != VK_NULL_HANDLE) {
            pipelines[key] = pipeline;
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

Stats GetStats() {
    Stats s;
    s.draws = draws;
    s.pipelines = pipelines.size();
    s.passes = passes;
    s.skipped = skipped;
    return s;
}

}  // namespace VkDraw
