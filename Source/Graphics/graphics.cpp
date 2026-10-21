//-----------------------------------------------------------------------------
//           Name: graphics.cpp
//      Developer: Wolfire Games LLC
//    Modified By: Overgrowth Online (crxhvrd)
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#include "graphics.h"

#include <map>
#include <set>
#include <chrono>
#include <deque>
#include <sstream>

#include <Internal/filesystem.h>
#include <Internal/integer.h>
#include <Internal/config.h>
#include <Internal/datemodified.h>
#include <Internal/hardware_specs.h>
#include <Internal/profiler.h>
#include <Internal/common.h>
#include <Internal/detect_settings.h>

#include <Graphics/vboringcontainer.h>
#include <Graphics/textures.h>
#include <Graphics/vbocontainer.h>
#include <Graphics/shaders.h>
#include <Graphics/camera.h>
#include <Graphics/frame_timers.h>
#include <Graphics/models.h>
#include <Graphics/flares.h>
#include <Graphics/render_backend.h>
#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_draw.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Graphics/Vulkan/vk_queries.h>

#include <Utility/sdl_util.h>
#include <Utility/assert.h>

#include <Timing/timingevent.h>
#include <Timing/intel_gl_perf.h>

#include <Images/image_export.hpp>
#include <Math/vec3math.h>
#include <Main/scenegraph.h>
#include <Logging/logdata.h>
#include <Objects/decalobject.h>
#include <Main/engine.h>

#include <SDL.h>

#include <memory.h>
#include <cstdio>

#ifdef _WIN32
// Request more powerful graphics card if on a laptop with both integrated and discrete graphics
extern "C" {
_declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

extern SceneLight* primary_light;

#if PLATFORM_MACOSX
#include <OpenGL/OpenGL.h>
#endif

bool g_level_shadows = true;
bool g_simple_shadows = true;
bool g_detail_objects_reduced = true;
bool g_detail_objects_reduced_dirty = false;
bool g_albedo_only = false;
bool g_disable_fog = false;
bool g_no_reflection_capture = false;
bool g_no_decals = false;
bool g_no_decal_elements = false;
bool g_character_decals_enabled = false;  // Note: This is enabled via 'Custom Shaders' level param. No config value for it
bool g_no_detailmaps = false;
bool g_single_pass_shadow_cascade = false;
bool g_simple_water = false;
bool g_particle_field_simple = false;
bool g_draw_vr = false;
bool g_s3tc_dxt5_support = false;
bool g_s3tc_dxt5_textures = true;
bool g_opengl_callback_error_dialog = true;
bool g_perform_occlusion_query = false;
std::string VulkanStartGuard();  // Overgrowth Online (stage 5.8; below)
static void LogSlowFrame();      // (below)
bool g_gamma_correct_final_output = true;
bool g_attrib_envobj_instancing_support = false;
bool g_attrib_envobj_instancing_enabled = true;
bool g_ubo_batch_multiplier_force_1x = false;

// Variables for turning off features at runtime for testing performance. Not backed by config, so as not to pollute it
bool g_debug_runtime_disable_blood_surface_pre_draw = false;
bool g_debug_runtime_disable_debug_draw = false;
bool g_debug_runtime_disable_debug_ribbon_draw = false;
bool g_debug_runtime_disable_decal_object_draw = false;
bool g_debug_runtime_disable_decal_object_pre_draw_frame = false;
bool g_debug_runtime_disable_detail_object_surface_draw = false;
bool g_debug_runtime_disable_detail_object_surface_pre_draw = false;
bool g_debug_runtime_disable_dynamic_light_object_draw = false;
bool g_debug_runtime_disable_env_object_draw = false;
bool g_debug_runtime_disable_env_object_draw_depth_map = false;
bool g_debug_runtime_disable_env_object_draw_detail_object_instances = false;
bool g_debug_runtime_disable_env_object_draw_instances = false;
bool g_debug_runtime_disable_env_object_draw_instances_transparent = false;
bool g_debug_runtime_disable_env_object_pre_draw_camera = false;
bool g_debug_runtime_disable_flares_draw = false;
bool g_debug_runtime_disable_gpu_particle_field_draw = false;
bool g_debug_runtime_disable_group_pre_draw_camera = false;
bool g_debug_runtime_disable_group_pre_draw_frame = false;
bool g_debug_runtime_disable_hotspot_draw = false;
bool g_debug_runtime_disable_hotspot_pre_draw_frame = false;
bool g_debug_runtime_disable_item_object_draw = false;
bool g_debug_runtime_disable_item_object_draw_depth_map = false;
bool g_debug_runtime_disable_item_object_pre_draw_frame = false;
bool g_debug_runtime_disable_morph_target_pre_draw_camera = false;
bool g_debug_runtime_disable_movement_object_draw = false;
bool g_debug_runtime_disable_movement_object_draw_depth_map = false;
bool g_debug_runtime_disable_movement_object_pre_draw_camera = false;
bool g_debug_runtime_disable_movement_object_pre_draw_frame = false;
bool g_debug_runtime_disable_navmesh_connection_object_draw = false;
bool g_debug_runtime_disable_navmesh_hint_object_draw = false;
bool g_debug_runtime_disable_particle_draw = false;
bool g_debug_runtime_disable_particle_system_draw = false;
bool g_debug_runtime_disable_pathpoint_object_draw = false;
bool g_debug_runtime_disable_placeholder_object_draw = false;
bool g_debug_runtime_disable_reflection_capture_object_draw = false;
bool g_debug_runtime_disable_rigged_object_draw = false;
bool g_debug_runtime_disable_rigged_object_pre_draw_camera = false;
bool g_debug_runtime_disable_rigged_object_pre_draw_frame = false;
bool g_debug_runtime_disable_scene_graph_draw = false;
bool g_debug_runtime_disable_scene_graph_draw_depth_map = false;
bool g_debug_runtime_disable_scene_graph_prepare_lights_and_decals = false;
bool g_debug_runtime_disable_sky_draw = false;
bool g_debug_runtime_disable_terrain_object_draw_depth_map = false;
bool g_debug_runtime_disable_terrain_object_draw_terrain = false;
bool g_debug_runtime_disable_terrain_object_pre_draw_camera = false;

unsigned int Graphics::GetShadowSize(unsigned int target_shadow_size) {
    unsigned int shadow_size = 8;
    while (shadow_size < target_shadow_size) {
        shadow_size *= 2;
    }
    shadow_size /= 2;

    shadow_size /= config_.texture_reduction_factor();

    const unsigned int kMaxShadowSize = 1024;
    if (shadow_size > kMaxShadowSize) {
        shadow_size = kMaxShadowSize;
    }
    if (shadow_size < 1) {
        shadow_size = 1;
    }
    return shadow_size;
}

// Overgrowth Online (GL 4.6 stage 5.4): a render target's attachments by their
// textures' GL-style names, for the Vulkan draws
static VkDraw::Target VkTargetOf(const RenderTarget& target) {
    VkDraw::Target out;
    Textures* textures = Textures::Instance();
    for (int i = 0; i < target.num_color && i < VkDraw::Target::kMaxColor; ++i) {
        if (target.color[i].tex.valid()) {
            textures->EnsureInVRAM(target.color[i].tex);
            out.color[i].texture = textures->returnTexture(target.color[i].tex);
        }
        out.color[i].level = target.color[i].level;
        out.color[i].layer = target.color[i].layer;
    }
    out.num_color = target.num_color;
    if (target.depth.tex.valid()) {
        textures->EnsureInVRAM(target.depth.tex);
        out.depth.texture = textures->returnTexture(target.depth.tex);
        out.depth.level = target.depth.level;
        out.depth.layer = target.depth.layer;
    }
    return out;
}

// Clear the framebuffer
void Graphics::Clear(const bool clear_color_b) {
    setDepthWrite(true);
    if (UseVulkan()) {
        VkDraw::Clear(clear_color_b, true, clear_color_, shadow_state_.scissor_test_, gl_scissor_);
        return;
    }

    // Only clear the color buffer if will not be overdrawn anyways
    if (clear_color_b)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    else
        glClear(GL_DEPTH_BUFFER_BIT);
}

void Graphics::setPolygonOffset(bool new_polygon_offset) {
    if (new_polygon_offset == shadow_state_.polygon_offset_enable_) return;
    shadow_state_.polygon_offset_enable_ = new_polygon_offset;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        shadow_state_.polygon_offset_enable_ ? glEnable(GL_POLYGON_OFFSET_FILL) : glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void Graphics::setAlphaToCoverage(const bool alpha_to_coverage) {
    if (alpha_to_coverage == shadow_state_.alpha_to_coverage_) return;
    shadow_state_.alpha_to_coverage_ = alpha_to_coverage;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        alpha_to_coverage ? glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE) : glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
}

void Graphics::setScissorTest(const bool scissor_test) {
    if (scissor_test == shadow_state_.scissor_test_) return;
    shadow_state_.scissor_test_ = scissor_test;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        scissor_test ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }
}

void Graphics::setColorMask(const bool red, const bool green, const bool blue, const bool alpha) {
    unsigned char mask = (red ? 1 : 0) | (green ? 2 : 0) | (blue ? 4 : 0) | (alpha ? 8 : 0);
    if (mask == shadow_state_.color_mask_) return;
    shadow_state_.color_mask_ = mask;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        glColorMask(red, green, blue, alpha);
    }
}

void Graphics::setSimpleShadows(const bool val) {
    g_simple_shadows = val;
    if (initialized) {
        InitScreen();
    }
}

void Graphics::setSimpleWater(const bool val) {
    g_simple_water = val || g_draw_vr;
}

void Graphics::SetParticleFieldSimple(bool val) {
    g_particle_field_simple = val;
}

void Graphics::setAttribEnvObjInstancing(bool val) {
    g_attrib_envobj_instancing_enabled = val;
}

void Graphics::PushViewport() {
    ViewportDims dims;
    for (int i = 0; i < 4; ++i) {
        dims.entries[i] = viewport_dim[i];
    }
    viewport_dims_stack.push(dims);
}

void Graphics::PopViewport() {
    LOG_ASSERT(!viewport_dims_stack.empty());
    ViewportDims dims = viewport_dims_stack.top();
    viewport_dims_stack.pop();
    for (int i = 0; i < 4; ++i) {
        viewport_dim[i] = dims.entries[i];
        gl_viewport_[i] = gl_scissor_[i] = viewport_dim[i];
    }
    if (!UseVulkan()) {  // (Vulkan: the draws read gl_viewport_/gl_scissor_)
        glScissor(viewport_dim[0], viewport_dim[1], viewport_dim[2], viewport_dim[3]);
        glViewport(viewport_dim[0], viewport_dim[1], viewport_dim[2], viewport_dim[3]);
    }
}

void Graphics::setBlend(const bool new_blend) {
    GLState& gl_state_ = shadow_state_.gl_state_;
    if (new_blend == gl_state_.blend) return;
    gl_state_.blend = !gl_state_.blend;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        gl_state_.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    }
}

void Graphics::setDepthTest(const bool new_depth_test) {
    GLState& gl_state_ = shadow_state_.gl_state_;
    if (new_depth_test == gl_state_.depth_test) return;
    gl_state_.depth_test = !gl_state_.depth_test;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        gl_state_.depth_test ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }
}

void Graphics::setCullFace(const bool new_cull_face) {
    GLState& gl_state_ = shadow_state_.gl_state_;
    if (new_cull_face == gl_state_.cull_face) return;
    gl_state_.cull_face = !gl_state_.cull_face;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        gl_state_.cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }
}

void Graphics::setDepthWrite(const bool new_depth_write) {
    GLState& gl_state_ = shadow_state_.gl_state_;
    if (new_depth_write == gl_state_.depth_write) return;
    gl_state_.depth_write = !gl_state_.depth_write;
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        gl_state_.depth_write ? glDepthMask(1) : glDepthMask(0);
    }
}

void Graphics::setGLState(const GLState& state) {
    setDepthTest(state.depth_test);
    setCullFace(state.cull_face);
    setBlend(state.blend);
    setDepthWrite(state.depth_write);
    SetBlendFunc(state.blend_src, state.blend_dst, state.blend_alpha_src, state.blend_alpha_dst);
}

void Graphics::StartTextureSpaceRenderingCustom(int x, int y, int width, int height) {
    PushViewport();
    setViewport(x, y, x + width, y + height);
    CHECK_GL_ERROR();
}

void Graphics::EndTextureSpaceRendering() {
    PopViewport();
}

// Overgrowth Online (GL 4.6 stages 2.5, 5.0): render targets (see graphics.h)
bool Graphics::RenderTargetKey::operator==(const RenderTargetKey& o) const {
    for (int i = 0; i <= RenderTarget::kMaxColor; ++i) {
        if (names[i] != o.names[i] || levels[i] != o.levels[i] || layers[i] != o.layers[i]) {
            return false;
        }
    }
    return true;
}

static void SetDrawBuffersOf(GLuint fbo, int count) {
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        return;
    }
    GLenum bufs[RenderTarget::kMaxColor];
    for (int i = 0; i < RenderTarget::kMaxColor; ++i) {
        bufs[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    if (count > 0) {
        glNamedFramebufferDrawBuffers(fbo, count, bufs);
    } else {
        glNamedFramebufferDrawBuffer(fbo, GL_NONE);
    }
}

GLuint Graphics::FramebufferFor(const RenderTarget& target) {
    if (target.IsWindow()) {
        return 0;
    }
    Textures* textures = Textures::Instance();
    RenderTargetKey key;
    for (int i = 0; i <= RenderTarget::kMaxColor; ++i) {
        const RenderTarget::Attachment* a = i < RenderTarget::kMaxColor ? (i < target.num_color ? &target.color[i] : NULL) : &target.depth;
        key.names[i] = 0;
        key.levels[i] = 0;
        key.layers[i] = -1;
        if (a != NULL && a->tex.valid()) {
            textures->EnsureInVRAM(a->tex);
            key.names[i] = textures->returnTexture(a->tex);
            key.levels[i] = a->level;
            key.layers[i] = a->layer;
        }
    }
    for (const CachedFramebuffer& c : render_targets_) {
        if (c.key == key) {
            return c.fbo;
        }
    }
    GLuint fbo = 0;
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        fbo = VkStub::NewId();
    } else {
        glCreateFramebuffers(1, &fbo);
        for (int i = 0; i <= RenderTarget::kMaxColor; ++i) {
            if (key.names[i] == 0) {
                continue;
            }
            GLenum attachment = i < RenderTarget::kMaxColor ? GL_COLOR_ATTACHMENT0 + i : GL_DEPTH_ATTACHMENT;
            if (key.layers[i] < 0) {
                glNamedFramebufferTexture(fbo, attachment, key.names[i], key.levels[i]);
            } else {
                glNamedFramebufferTextureLayer(fbo, attachment, key.names[i], key.levels[i], key.layers[i]);
            }
        }
        SetDrawBuffersOf(fbo, target.num_color);
        glNamedFramebufferReadBuffer(fbo, target.num_color > 0 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOGE << "Render target incomplete (0x" << std::hex << status << std::dec << "), " << target.num_color << " colour attachment(s)" << std::endl;
        }
    }
    CachedFramebuffer c;
    c.key = key;
    c.fbo = fbo;
    c.draw_count = target.num_color;
    render_targets_.push_back(c);
    return fbo;
}

void Graphics::BindRenderTarget(const RenderTarget& target) {
    current_target_ = target;
    current_fbo_ = FramebufferFor(target);
    if (UseVulkan()) {
        VkDraw::SetTarget(VkTargetOf(target));
        VkDraw::SetDrawCount(-1);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, current_fbo_);
    }
    current_draw_count_ = -1;
    SetDrawBuffers(target.num_color);
}

void Graphics::BindRenderTarget(TextureRef color, int color_level, TextureRef depth, int depth_level) {
    RenderTarget target;
    if (color.valid()) {
        target.Color(color, color_level);
    }
    if (depth.valid()) {
        target.Depth(depth, depth_level);
    }
    BindRenderTarget(target);
}

void Graphics::BindWindow() {
    BindRenderTarget(RenderTarget());
}

void Graphics::PushRenderTarget() {
    RenderTargetState s;
    s.target = current_target_;
    s.draw_count = current_draw_count_;
    render_target_stack.push(s);
}

void Graphics::PopRenderTarget() {
    RenderTargetState s = render_target_stack.top();
    render_target_stack.pop();
    BindRenderTarget(s.target);
    if (s.draw_count >= 0) {
        SetDrawBuffers(s.draw_count);
    }
}

void Graphics::SetDrawBuffers(int count) {
    if (current_fbo_ == 0) {
        return;  // (the window draws to its back buffer)
    }
    if (count == current_draw_count_) {
        return;
    }
    if (UseVulkan()) {
        VkDraw::SetDrawCount(count);
    }
    for (CachedFramebuffer& c : render_targets_) {
        if (c.fbo == current_fbo_) {
            if (c.draw_count != count) {
                SetDrawBuffersOf(c.fbo, count);
                c.draw_count = count;
            }
            break;
        }
    }
    current_draw_count_ = count;
}

void Graphics::SetClearColor(const vec4& color) {
    for (int i = 0; i < 4; ++i) {
        clear_color_[i] = color[i];
    }
    if (UseVulkan()) {
        return;
    }
    glClearColor(color[0], color[1], color[2], color[3]);
}

void Graphics::BlitRenderTarget(const RenderTarget& src, int src_color, const PixelRect& s,
                                const RenderTarget& dst, int dst_color, const PixelRect& d,
                                bool depth, bool linear) {
    GLuint src_fbo = FramebufferFor(src);
    GLuint dst_fbo = FramebufferFor(dst);
    GLbitfield mask = GL_DEPTH_BUFFER_BIT;
    if (!depth) {
        mask = GL_COLOR_BUFFER_BIT;
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glNamedFramebufferReadBuffer(src_fbo, src_fbo != 0 ? GL_COLOR_ATTACHMENT0 + src_color : GL_BACK);
            glNamedFramebufferDrawBuffer(dst_fbo, dst_fbo != 0 ? GL_COLOR_ATTACHMENT0 + dst_color : GL_BACK);
        }
        for (CachedFramebuffer& c : render_targets_) {
            if (c.fbo == dst_fbo) {
                c.draw_count = -1;  // (set again on its next bind)
            }
        }
        if (dst_fbo == current_fbo_) {
            current_draw_count_ = -1;
        }
    }
    if (UseVulkan()) {
        const int sr[4] = {s.x, s.y, s.x + s.w, s.y + s.h};
        const int dr[4] = {d.x, d.y, d.x + d.w, d.y + d.h};
        VkDraw::Blit(VkTargetOf(src), src_color, sr, VkTargetOf(dst), dst_color, dr, depth, linear);
        return;
    }
    bool scissor = shadow_state_.scissor_test_;
    setScissorTest(false);  // (a blit is clipped by the scissor box otherwise)
    if (!UseVulkan()) {  // (Vulkan: stage 5.5)
        glBlitNamedFramebuffer(src_fbo, dst_fbo, s.x, s.y, s.x + s.w, s.y + s.h, d.x, d.y, d.x + d.w, d.y + d.h,
                               mask, (linear && !depth) ? GL_LINEAR : GL_NEAREST);
    }
    setScissorTest(scissor);
    if (!depth && src_fbo != 0 && !UseVulkan()) {  // (Vulkan: stage 5.5)
        glNamedFramebufferReadBuffer(src_fbo, GL_COLOR_ATTACHMENT0);
    }
    if (dst_fbo == current_fbo_ && current_draw_count_ < 0) {
        SetDrawBuffers(current_target_.num_color);
    }
}

void Graphics::ResolveMultisample() {
    BlitColorBuffer();
    BlitDepthBuffer();
}

// The bytes glReadPixels writes per pixel (what the Vulkan stub zero-fills)
static size_t ReadPixelBytes(GLenum format, GLenum type) {
    switch (type) {
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_5_9_9_9_REV:
        case GL_UNSIGNED_INT_24_8:
            return 4;
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV:
            return 2;
        case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
            return 8;
    }
    size_t components = 4;
    switch (format) {
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_RED_INTEGER:
        case GL_DEPTH_COMPONENT:
        case GL_STENCIL_INDEX:
            components = 1;
            break;
        case GL_RG:
        case GL_RG_INTEGER:
            components = 2;
            break;
        case GL_RGB:
        case GL_BGR:
        case GL_RGB_INTEGER:
        case GL_BGR_INTEGER:
            components = 3;
            break;
    }
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
            return components;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:
            return components * 2;
        default:
            return components * 4;  // (GL_FLOAT, GL_INT, GL_UNSIGNED_INT)
    }
}

void Graphics::ReadPixels(const PixelRect& r, GLenum format, GLenum type, void* out, int color) {
    if (UseVulkan()) {
        if (r.w > 0 && r.h > 0) {
            size_t row = (size_t)r.w * ReadPixelBytes(format, type);
            size_t stride = (row + 3) & ~(size_t)3;  // (GL_PACK_ALIGNMENT: 4, never changed)
            VkDraw::ReadPixels(r.x, r.y, r.w, r.h, format, type, out, stride * (size_t)(r.h - 1) + row, color);
        }
        return;
    }
    glNamedFramebufferReadBuffer(current_fbo_, current_fbo_ != 0 ? GL_COLOR_ATTACHMENT0 + color : GL_BACK);
    glReadPixels(r.x, r.y, r.w, r.h, format, type, out);
}

void Graphics::BindUniformRing(int binding, const UniformRingBuffer& ring) {
    if (UseVulkan()) {
        VkDraw::SetUniformBuffer(binding, ring.gl_id, ring.offset, ring.next_offset - ring.offset);
        return;
    }
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, ring.gl_id, ring.offset, ring.next_offset - ring.offset);
}

void Graphics::BindUniformData(int binding, const void* data, size_t size) {
    GLint align = 256;
    GLVK(GetIntegerv)(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    FrameRing::Allocation a = FrameRing::Allocate(size, (size_t)align);
    memcpy(a.ptr, data, size);
    if (UseVulkan()) {
        VkDraw::SetUniformBuffer(binding, a.buffer, a.offset, size);
    } else {
        glBindBufferRange(GL_UNIFORM_BUFFER, binding, a.buffer, a.offset, size);
    }
}

int Graphics::MaxUniformBlockSize() {
    if (max_uniform_block_size_ <= 0) {
        GLVK(GetIntegerv)(GL_MAX_UNIFORM_BLOCK_SIZE, &max_uniform_block_size_);
    }
    return max_uniform_block_size_;
}

int Graphics::MaxTextureBufferSize() {
    if (max_texture_buffer_size_ <= 0) {
        GLVK(GetIntegerv)(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texture_buffer_size_);
    }
    return max_texture_buffer_size_;
}

GLuint Graphics::CreateQuery(QueryKind kind) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        return VkQueries::Create(kind == kTimestampQuery);
    }
    GLuint q = 0;
    glCreateQueries(kind == kTimestampQuery ? GL_TIMESTAMP : GL_SAMPLES_PASSED, 1, &q);
    return q;
}

void Graphics::DeleteQuery(GLuint query) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkQueries::Delete(query);
        return;
    }
    glDeleteQueries(1, &query);
}

void Graphics::BeginOcclusionQuery(GLuint query) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkQueries::BeginOcclusion(query);
        return;
    }
    glBeginQuery(GL_SAMPLES_PASSED, query);
}

void Graphics::EndOcclusionQuery() {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkQueries::EndOcclusion();
        return;
    }
    glEndQuery(GL_SAMPLES_PASSED);
}

void Graphics::WriteTimestamp(GLuint query) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkQueries::WriteTimestamp(query);
        return;
    }
    glQueryCounter(query, GL_TIMESTAMP);
}

bool Graphics::QueryAvailable(GLuint query) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        return VkQueries::Available(query);
    }
    GLuint available = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
}

uint64_t Graphics::QueryResult(GLuint query) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        return VkQueries::Result(query);
    }
    GLuint64 result = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &result);
    return (uint64_t)result;
}

void Graphics::PushDebugGroup(const char* name) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkBackend::BeginLabel(name);
        return;
    }
    if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    }
}

void Graphics::PopDebugGroup() {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkBackend::EndLabel();
        return;
    }
    if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) {
        glPopDebugGroup();
    }
}

void Graphics::SetWireframe(bool wireframe) {
    if (wireframe != wireframe_) {
        if (!UseVulkan()) {  // (Vulkan: the draw state's)
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        }
        wireframe_ = wireframe;
    }
}

void Graphics::SetPointSize(float size) {
    if (size != point_size_) {
        if (!UseVulkan()) {  // (Vulkan: the shaders write gl_PointSize)
            glPointSize(size);
        }
        point_size_ = size;
    }
}

void Graphics::SetScissorRect(int x, int y, int width, int height) {
    gl_scissor_[0] = x;
    gl_scissor_[1] = y;
    gl_scissor_[2] = width;
    gl_scissor_[3] = height;
    if (UseVulkan()) {
        return;
    }
    glScissor(x, y, width, height);
}

void Graphics::SetViewportOnly(int x, int y, int width, int height) {
    gl_viewport_[0] = x;
    gl_viewport_[1] = y;
    gl_viewport_[2] = width;
    gl_viewport_[3] = height;
    if (UseVulkan()) {
        return;
    }
    glViewport(x, y, width, height);
}

void Graphics::SetViewportIndexed(int index, int x, int y, int width, int height) {
    if (index == 0) {
        gl_viewport_[0] = gl_scissor_[0] = x;
        gl_viewport_[1] = gl_scissor_[1] = y;
        gl_viewport_[2] = gl_scissor_[2] = width;
        gl_viewport_[3] = gl_scissor_[3] = height;
    }
    if (UseVulkan()) {  // (Vulkan: viewport 0 only, stage 5.5)
        return;
    }
    glScissorIndexed(index, x, y, width, height);
    glViewportIndexedf(index, (float)x, (float)y, (float)width, (float)height);
}

void Graphics::ClearColorOnly() {
    if (UseVulkan()) {
        VkDraw::Clear(true, false, clear_color_, shadow_state_.scissor_test_, gl_scissor_);
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT);
}

void Graphics::Flush() {
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        return;
    }
    glFlush();
}

void Graphics::ForgetRenderTargets(GLuint texture_name) {
    for (size_t i = 0; i < render_targets_.size();) {
        bool uses = false;
        for (int a = 0; a <= RenderTarget::kMaxColor; ++a) {
            uses = uses || render_targets_[i].key.names[a] == texture_name;
        }
        if (uses) {
            if (current_fbo_ == render_targets_[i].fbo) {
                if (!UseVulkan()) {  // (Vulkan: stage 5.5)
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                }
                current_fbo_ = 0;
                current_target_ = RenderTarget();
            }
            if (!UseVulkan()) {  // (Vulkan: stage 5.5)
                glDeleteFramebuffers(1, &render_targets_[i].fbo);
            }
            render_targets_[i] = render_targets_.back();
            render_targets_.pop_back();
        } else {
            ++i;
        }
    }
}

void Graphics::BindVBO(int target, int val) {
    switch (target) {
        case GL_ARRAY_BUFFER:
            LOGS << "Binding array VBO " << val << std::endl;
            ;
            BindArrayVBO(val);
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            LOGS << "Binding element VBO " << val << std::endl;
            ;
            BindElementVBO(val);
            break;
        default:
            // No valid target
            SDL_assert(false);
            break;
    }
}

// Helping function to explicitally unbind a buffer (if it's bound)
// This will prevent the rebind-saving routine from messing up the state.
void Graphics::UnbindVBO(int target, int val) {
    if (target == GL_ARRAY_BUFFER && vbo_array_bound == val) {
        BindVBO(GL_ARRAY_BUFFER, 0);
    }

    if (target == GL_ELEMENT_ARRAY_BUFFER && vbo_element_bound == val) {
        BindVBO(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

// Overgrowth Online (GL 4.6 stage 2.4): code that sets up the engine's VAO while a
// layout's is still bound (a missing EndVertexLayout) would change the layout's
static const char* g_layout_label = "";  // the layout begun last (its last input, for the message)
static void LayoutLeftBound(Graphics* graphics, const char* where) {
    static int warned = 0;
    if (warned < 8) {
        LOGE << "A vertex layout was still bound when the engine's VAO was used (missing EndVertexLayout): " << g_layout_label << " at " << where << std::endl;
        ++warned;
    }
    graphics->EndVertexLayout();
}

void Graphics::BindArrayVBO(int val) {
    if (vertex_layout_bound_) {
        LayoutLeftBound(this, "BindArrayVBO");
    }
    LOGS << "Current vbo_array_bound " << vbo_array_bound << std::endl;
    LOGS << "Value " << val << std::endl;
    if (vbo_array_bound != val) {
        vbo_array_bound = val;
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glBindBuffer(GL_ARRAY_BUFFER, val);
        }
    } else {
        LOGS << "Tried to rebind " << val << std::endl;
    }
}

void Graphics::BindElementVBO(int val) {
    if (vertex_layout_bound_) {
        LayoutLeftBound(this, "BindElementVBO");
    }
    if (vbo_element_bound != val) {
        vbo_element_bound = val;
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, val);
        }
    }
}

// Start the scene drawing by clearing and setting the matrixmode
void Graphics::startDraw(vec2 start, vec2 end, ScreenType screen_type) {
    PROFILER_GPU_ZONE(g_profiler_ctx, "Graphics::startDraw()");
    if (screen_type == kRender) {
        setViewport((GLint)((float)render_dims[0] * start[0]),
                    (GLint)((float)render_dims[1] * start[1]),
                    (GLint)((float)render_dims[0] * end[0]),
                    (GLint)((float)render_dims[1] * end[1]));
    } else if (screen_type == kWindow) {
        setViewport((GLint)((float)window_dims[0] * start[0]),
                    (GLint)((float)window_dims[1] * start[1]),
                    (GLint)((float)window_dims[0] * end[0]),
                    (GLint)((float)window_dims[1] * end[1]));
    } else if (screen_type == kTexture) {
        setViewport((GLint)((float)start[0]),
                    (GLint)((float)start[1]),
                    (GLint)((float)end[0]),
                    (GLint)((float)end[1]));
    }
}

void Graphics::TakeScreenshot() {
    unsigned long width = window_dims[0];
    unsigned long height = window_dims[1];

    unsigned long size = width * height;
    unsigned char* pixels = new unsigned char[size * 4];
    ::memset(pixels, 0, size * 4);

    // (Overgrowth Online stage 5.0: the window's back buffer, through ReadPixels)
    PushRenderTarget();
    BindWindow();
    ReadPixels(PixelRect(0, 0, (int)width, (int)height), GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    PopRenderTarget();

    if (screenshot_mode == kGameplay) {
        int index3 = 0;
        int index4 = 0;
        for (unsigned i = 0; i < width * height; i++) {
            pixels[index3 + 0] = pixels[index4 + 0];
            pixels[index3 + 1] = pixels[index4 + 1];
            pixels[index3 + 2] = pixels[index4 + 2];
            index3 += 3;
            index4 += 4;
        }
        std::string path = queued_screenshot_path;
        queued_screenshot_path.clear();

        if (path.empty()) {
            path = ImageExport::FindEmptySequentialFile("Screenshots/OvergrowthScreenshot", ".jpg");
        }

        // Overgrowth Online: ".png": lossless (screenshot comparisons), upright and RGB
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0) {
            ImageExport::SaveScreenshotPNG(path.c_str(), pixels, width, height);
        } else {
            ImageExport::SaveJPEG(path.c_str(), pixels, width, height);
        }
    } else if (screenshot_mode == kTransparentGameplay) {
        std::string path = ImageExport::FindEmptySequentialFile("Screenshots/OvergrowthScreenshot", ".png");
        ImageExport::SavePNGTransparent(path.c_str(), pixels, width, height);
    }

    delete[] pixels;
}

// End the scene drawing by drawing the backbuffer to the screen
void Graphics::SwapToScreen() {
    LOG_ASSERT(viewport_dims_stack.empty());
    PROFILER_GPU_ZONE(g_profiler_ctx, "Graphics::SwapToScreen()");  // glFlush(); Unnecessary on Mac OS X, as CGLFlushDrawable auto-flushes the command buffer

    // Overgrowth Online: the finished frame, before the swap. After it the back
    // buffer is undefined, and overlays that hook the swap (RivaTuner/Afterburner,
    // Steam) have drawn into it: screenshots came out with their OSD
    if (queued_screenshot == 2) {
        TakeScreenshot();
        queued_screenshot = 0;
        SetMediaMode(pre_screenshot_media_mode_state);
    }

    if (UseVulkan()) {  // Overgrowth Online (GL 4.6 stage 5.2): the window's frame, flipped
        PROFILER_ZONE_STALL(g_profiler_ctx, "Vulkan present");
        VkDraw::Present();
        LogSlowFrame();
        if (VkBackend::FramesPresented() == 120) {
            deletefile(VulkanStartGuard().c_str());  // (it started: see InitScreen)
        }
    } else {
        PROFILER_ZONE_STALL(g_profiler_ctx, "SDL_GL_SwapWindow");
        SDL_GL_SwapWindow(sdl_window_);
        GL_SWAP();
        LogSlowFrame();
    }
    FrameRing::EndFrame();  // Overgrowth Online (GL 4.6 stage 2.3)

    if (queued_screenshot) {
        ++queued_screenshot;
    }

    settings_changed = false;
}

// Set area of screen to draw to
void Graphics::setViewport(const GLint startx, const GLint starty, const GLint endx, const GLint endy) {
    viewport_dim[0] = startx;
    viewport_dim[1] = starty;
    viewport_dim[2] = endx - startx;
    viewport_dim[3] = endy - starty;
    for (int i = 0; i < 4; ++i) {
        gl_viewport_[i] = gl_scissor_[i] = viewport_dim[i];
    }
    if (!UseVulkan()) {  // (Vulkan: the draws read gl_viewport_/gl_scissor_)
        glScissor(viewport_dim[0], viewport_dim[1], viewport_dim[2], viewport_dim[3]);
        glViewport(viewport_dim[0], viewport_dim[1], viewport_dim[2], viewport_dim[3]);
    }
}

void Graphics::setAdditiveBlend(const bool additive) {
    if (additive)
        SetBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        SetBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Graphics::SetBlendFunc(const int src, const int dst, const int alpha_src, const int alpha_dst) {
    GLState& gl_state_ = shadow_state_.gl_state_;
    if (src != gl_state_.blend_src || dst != gl_state_.blend_dst || alpha_src != gl_state_.blend_alpha_src || alpha_dst != gl_state_.blend_alpha_dst) {
        // glBlendFunc(src,dst);
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glBlendFuncSeparate(src, dst, alpha_src, alpha_dst);
        }
        gl_state_.blend_src = src;
        gl_state_.blend_dst = dst;
        gl_state_.blend_alpha_src = alpha_src;
        gl_state_.blend_alpha_dst = alpha_dst;
    }
}

void Graphics::setDepthFunc(const int type) {
    if (shadow_state_.depth_func_unset_ || type != shadow_state_.depth_func_type_) {
        // Overgrowth Online (GL 4.6 stage 4a): reversed-Z, nearer is greater. The engine
        // asks in the usual terms (LESS = nearer passes) and gets the reversed test
        GLenum reversed = type;
        switch (type) {
            case GL_LESS: reversed = GL_GREATER; break;
            case GL_LEQUAL: reversed = GL_GEQUAL; break;
            case GL_GREATER: reversed = GL_LESS; break;
            case GL_GEQUAL: reversed = GL_LEQUAL; break;
        }
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glDepthFunc(reversed);
        }
        shadow_state_.depth_func_type_ = type;
        shadow_state_.depth_func_unset_ = false;
    }
}

Graphics::Graphics() : gl_lib_loaded_(false),
                       depth_prepass(true),
                       shadow_size(20),
                       cascade_shadow_res(4096),
                       shadow_res(1024 * 2),
                       vbo_array_bound(-1),
                       vbo_element_bound(-1),
                       multisample_framebuffer_exists(false),
                       framebuffer_exists(false),
                       old_FSAA_samples(0),
                       old_post_effects_enable(false),
                       first_load(true),
                       initialized(false),
                       settings_changed(false),
                       sdl_window_(NULL),
                       gl_context(NULL),
                       active_vertex_attrib_arrays(0),
                       wanted_vertex_attrib_arrays(0),
                       max_vertex_attrib_arrays(0),
                       current_fbo_(0),
                       current_draw_count_(-1),
                       point_size_(-1.0f),
                       wireframe_(false),
                       max_uniform_block_size_(0),
                       max_texture_buffer_size_(0),
                       shader("post") {
    old_render_dims[0] = 0;
    old_render_dims[1] = 0;
    config_.SetDetailObjectDecals(true);
    config_.SetDetailObjectShadows(true);
    config_.SetDetailObjectLowres(false);
    config_.SetSimpleFog(false);
    config_.SetSplitScreen(false);
    config_.SetFSAASamples(0);
    config_.SetAnisotropy(0);
    config_.SetTargetMonitor(0);
    config_.SetFullscreen(FullscreenMode::kWindowed);
    config_.SetVSync(false);
    config_.SetLimitFpsInGame(false);
    config_.SetMaxFrameRate(60);
    config_.dynamic_character_cubemap_ = false;
    shadow_state_.depth_func_unset_ = true;
    media_mode_ = false;
    memset(shadow_state_.client_states_, 0, sizeof(shadow_state_.client_states_));
    shadow_state_.client_active_textures_ = 0;
}

void Graphics::Initialize() {
    initialized = true;
}

void Graphics::GetShaderNames(std::map<std::string, int>& preload_shaders) {
    preload_shaders[shader] = 0;
}

void Graphics::WindowResized(ivec2 value) {
    if (config_.full_screen() == FullscreenMode::kWindowed) {
        CheckForWindowResize();
    }
}

void Graphics::Dispose() {
    GL_PERF_FINALIZE();

    static_shadow_depth_ref.clear();
    cascade_shadow_depth_ref.clear();
    cascade_shadow_color_ref.clear();

    screen_color_tex.clear();
    screen_vel_tex.clear();
    screen_depth_tex.clear();

    post_effects.temp_screen_tex.clear();
    post_effects.tone_mapped_tex.clear();

    if (UseVulkan()) {
        VkBackend::Shutdown();  // (before the window: its surface)
    }
    if (gl_context) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }

    if (sdl_window_) {
        SDL_DestroyWindow(sdl_window_);
        sdl_window_ = NULL;
    }
}

const char* glErrorString(GLenum errCode) {
    switch (errCode) {
        case GL_NO_ERROR:
            return "No error";
        case GL_INVALID_ENUM:
            return "Invalid enum";
        case GL_INVALID_VALUE:
            return "Invalid value";
        case GL_INVALID_OPERATION:
            return "Invalid operation";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "Invalid framebuffer operation";
        case GL_OUT_OF_MEMORY:
            return "Out of memory";
        case GL_STACK_UNDERFLOW:
            return "Stack underflow";
        case GL_STACK_OVERFLOW:
            return "Stack overflow";
        default:
            return "Unknown GL error code";
    }
}

bool CheckGLError(int line, const char* file, const char* exterrmsg) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        return false;
    }
    GLenum errCode;
    const char* errString;
    errCode = glGetError();
    if (errCode != GL_NO_ERROR) {
        errString = glErrorString(errCode);
        char error_msg[1024];
        int i = 0;
        int last_slash = 0;
        while (file[i] != '\0') {
            if (file[i] == '\\' || file[i] == '/') last_slash = i + 1;
            i++;
        }
        if (exterrmsg) {
            FormatString(error_msg, 1024, "%s\n%s", exterrmsg, errString);
        } else {
            FormatString(error_msg, 1024, "On line %d of %s: \n%s", line, &file[last_slash], errString);
        }
        DisplayError("OpenGL error", error_msg);
        return true;
    } else {
        return false;
    }
}

bool CheckGLErrorStr(char* output, unsigned length) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        return false;
    }
    GLenum errCode;
    const char* errString;
    errCode = glGetError();
    if (errCode != GL_NO_ERROR) {
        errString = glErrorString(errCode);
        FormatString(output, length, "%s", errString);
        return true;
    }
    return false;
}

void CheckFBOError(int line, const char* file) {
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        return;
    }
    if (!glCheckFramebufferStatus) {
        return;
    }
    GLenum errCode;
    const int ERR_STRING_BUF_SIZE = 256;
    char errString[ERR_STRING_BUF_SIZE];
    errCode = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (errCode != GL_FRAMEBUFFER_COMPLETE) {
        switch (errCode) {
            case GL_FRAMEBUFFER_UNSUPPORTED:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - unsupported");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - incomplete attachment");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - missing attachment");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - incomplete dimensions");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - incomplete formats");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - incomplete draw buffer");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer - incomplete read buffer");
                break;
            default:
                FormatString(errString, ERR_STRING_BUF_SIZE, "Framebuffer error");
                break;
        }
        char error_msg[1024];
        int i = 0;
        int last_slash = 0;
        while (file[i] != '\0') {
            if (file[i] == '\\' || file[i] == '/') last_slash = i + 1;
            i++;
        }
        FormatString(error_msg, 1024, "On line %d of %s: \n%s", line, &file[last_slash], errString);
        DisplayError("OpenGL error", error_msg);
    }
}

void Graphics::BlitDepthBuffer() {
    Shaders::Instance()->setProgram(Shaders::Instance()->returnProgram(shader));
    GLState state;
    state.depth_test = true;
    state.depth_write = true;
    state.blend = false;
    state.cull_face = false;
    setGLState(state);
    // The above code is here to fix a mysterious problem
    // (Overgrowth Online stage 5.0: through BlitRenderTarget)
    PixelRect all(0, 0, render_dims[0], render_dims[1]);
    BlitRenderTarget(multisample_target, 0, all, scene_target, 0, all, true);
    Shaders::Instance()->noProgram();
}

static void ApplyVsync(bool val) {
    if (UseVulkan()) {  // (a present mode: the swapchain is made again)
        VkBackend::SetVsync(val);
        return;
    }
    // Set VSync
    if (val) {
        /* try late-swap-tearing first. If not supported, try normal vsync. */
        if (SDL_GL_SetSwapInterval(-1) == -1) {
            SDL_GL_SetSwapInterval(1);
        }
    } else {
        SDL_GL_SetSwapInterval(0); /* disable vsync. */
    }
}

void Graphics::SetAnisotropy(float val) {
    if (UseVulkan() || GLAD_GL_VERSION_4_6 || GLAD_GL_EXT_texture_filter_anisotropic) {
        static float max_anisotropy = -1.0f;
        if (max_anisotropy == -1.0f) {
            GLVK(GetFloatv)(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
        }
        // (Textures::ApplyAnisotropy sets it on every texture; this used to also
        // set it on whichever texture happened to be bound)
        config_.SetAnisotropy(min(val, max_anisotropy));
    } else {
        config_.SetAnisotropy(0.0f);
    }
}

// The window's size in pixels, on either renderer (Overgrowth Online stage 5.2)
static void GetDrawableSize(SDL_Window* window, int* w, int* h) {
    if (UseVulkan()) {
        VkBackend::GetDrawableSize(window, w, h);
    } else {
        SDL_GL_GetDrawableSize(window, w, h);
    }
}

static void SDL_GL_SetAttributeErrCheck(SDL_GLattr attr, int value) {
    if (SDL_GL_SetAttribute(attr, value) != 0) {
        FatalError("Error", "Could not set SDL_GL_attribute %d", (int)attr);
    }
}

void GLAPIENTRY amd_debug_callback(GLuint id, GLenum category, GLenum severity, GLsizei length, const GLchar* message, void* userParam) {
    LOGW << message << std::endl;
    if (g_opengl_callback_error_dialog) {
        DisplayError("GL debug error", message);
    }
}

const char* arb_debug_enum_string(GLenum val) {
    switch (val) {
        case GL_DEBUG_SOURCE_API_ARB:
            return "Source: API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
            return "Source: Window System";
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
            return "Source: Shader Compiler";
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:
            return "Source: Third Party";
        case GL_DEBUG_SOURCE_APPLICATION_ARB:
            return "Source: Application";
        case GL_DEBUG_SOURCE_OTHER_ARB:
            return "Source: Other";
        case GL_DEBUG_TYPE_ERROR_ARB:
            return "Type: Error";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
            return "Type: Deprecated Behavior";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
            return "Type: Undefined Behavior";
        case GL_DEBUG_TYPE_PORTABILITY_ARB:
            return "Type: Portability";
        case GL_DEBUG_TYPE_PERFORMANCE_ARB:
            return "Type: Performance";
        case GL_DEBUG_TYPE_OTHER_ARB:
            return "Type: Other";
        case GL_DEBUG_SEVERITY_HIGH_ARB:
            return "Severity: High";
        case GL_DEBUG_SEVERITY_MEDIUM_ARB:
            return "Severity: Medium";
        case GL_DEBUG_SEVERITY_LOW_ARB:
            return "Severity: Low";
        default:
            return "Unknown ARB Debug Enum";
    }
}

// Overgrowth Online: the GL's complaints, counted for the test bridge (gl_errors):
// errors, undefined and deprecated behaviour; each message once with its count
static int g_gl_debug_problem_count = 0;
static bool g_gl_debug_alive = false;  // our test marker came back: the output works
static std::map<std::string, int> g_gl_debug_problems;

static void NoteGLDebugMessage(GLenum type, GLuint id, const GLchar* message) {
    if (type != GL_DEBUG_TYPE_ERROR && type != GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR && type != GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR) {
        return;
    }
    ++g_gl_debug_problem_count;
    std::string key = std::to_string(id) + ": " + std::string(message ? message : "").substr(0, 200);
    if (g_gl_debug_problems.size() < 64 || g_gl_debug_problems.count(key)) {
        ++g_gl_debug_problems[key];
    }
}

bool GLDebugOutputAlive() {
    return g_gl_debug_alive;
}

int GLDebugProblemCount() {
    return g_gl_debug_problem_count;
}

std::map<std::string, int> GLDebugProblems() {
    return g_gl_debug_problems;
}

void GLAPIENTRY arb_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 0x4F4F) {
        g_gl_debug_alive = true;  // (Overgrowth Online: the check below)
        return;
    }
    const int kBufSize = 1024;
    char buf[kBufSize];
    FormatString(buf, kBufSize, "%s\n%s\nID: %d\n%s\n%s",
                 arb_debug_enum_string(source), arb_debug_enum_string(type),
                 id, arb_debug_enum_string(severity), message);

    if (source == GL_DEBUG_SOURCE_API_ARB && type == GL_DEBUG_TYPE_OTHER_ARB && id == 131185) {
        // LOGD << buf << std::endl;
        return;  // Ignore NVIDIA notifications that VBO will use Video RAM as source
    }
    if (source == GL_DEBUG_SOURCE_API_ARB && id == 131186) {
        // LOGD << buf << std::endl;
        return;  // Ignore NVIDIA notifications that VBO will use Video RAM as source
    }
    if (source == GL_DEBUG_SOURCE_API_ARB && type == GL_DEBUG_TYPE_PERFORMANCE_ARB && id == 131218) {
        LOGD << buf << std::endl;
        return;  // Ignore NVIDIA notifications that shader is going to be recompiled
        // because the shader key based on GL state mismatches, because I don't
        // know what to do about that right now
        // TODO: fix this and turn the warning back on
    }
    if (source == GL_DEBUG_SOURCE_API_ARB && type == GL_DEBUG_TYPE_PERFORMANCE_ARB && id == 2) {
        LOGD << buf << std::endl;
        return;  // Same as above but for Intel
    }
    if (source == GL_DEBUG_SOURCE_API_ARB && type == GL_DEBUG_TYPE_PERFORMANCE_ARB && id == 131154) {
        LOGD << buf << std::endl;
        return;  // Ignore NVIDIA warning that pixel transfer is synchronized with 3d rendering
    }

    LOGW << buf << std::endl;
    NoteGLDebugMessage(type, id, message);
    if (g_opengl_callback_error_dialog) {
        DisplayError("GL debug error", buf);
    }
}

extern float last_shadow_update_time;

// Set up the screen
void Graphics::InitScreen() {
    PROFILER_ZONE(g_profiler_ctx, "Graphics::InitScreen");
    Textures* textures = Textures::Instance();
    Shaders* shaders = Shaders::Instance();
    if (first_load) {
        PROFILER_ENTER(g_profiler_ctx, "Setting up driver and window");
        int n_video_driver = SDL_GetNumVideoDrivers();
        LOGI << "There are " << n_video_driver << " available video drivers." << std::endl;
        LOGI << "Video drivers are following:" << std::endl;

        for (int i = 0; i < n_video_driver; i++) {
            LOGI << "Video driver " << i << ": " << SDL_GetVideoDriver(i) << std::endl;
        }

        // Initialize default video driver
        if (SDL_VideoInit(NULL) < 0) {
            FatalError("Error", "Could not initialize default video driver");
        } else {
            LOGI << "Initialized video driver: " << SDL_GetCurrentVideoDriver() << std::endl;
        }
        // Overgrowth Online (GL 4.6 stage 5): config "renderer" = vulkan (or auto) starts
        // the Vulkan renderer, OpenGL when it can't start; anything else is OpenGL. (The
        // GL attributes are set either way: a failed Vulkan start makes a GL window.) A
        // Vulkan start that never reached its first frames (the game or the driver died
        // there) makes the next start OpenGL's, saved in the config (stage 5.8)
        const std::string renderer = config["renderer"].str();
        bool want_vulkan = renderer == "vulkan" || renderer == "auto";
        if (want_vulkan && fileexists(VulkanStartGuard().c_str()) == 0) {
            LOGE << "The last Vulkan start didn't reach its first frames: OpenGL from now on (Settings, Graphics, Renderer)" << std::endl;
            deletefile(VulkanStartGuard().c_str());
            config.GetRef("renderer") = std::string("gl");
            config.Save(GetConfigPath());
            want_vulkan = false;
        }
        if (want_vulkan) {
            createfile(VulkanStartGuard().c_str());
        }
        g_render_backend = want_vulkan && VkBackend::LoadVulkan() ? RenderBackend::kVulkan : RenderBackend::kGL;

        // Set GL attributes
        SDL_GL_SetAttributeErrCheck(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttributeErrCheck(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttributeErrCheck(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttributeErrCheck(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttributeErrCheck(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttributeErrCheck(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttributeErrCheck(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        // Overgrowth Online: OpenGL 4.6 core (docs_gl46_plan.md; it was 3.2)
        SDL_GL_SetAttributeErrCheck(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttributeErrCheck(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttributeErrCheck(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
        // Overgrowth Online: "gl_debug" asks for a debug context in any build
        // (the gl_errors tests; Release otherwise compiles the checks out)
#ifndef NO_GL_ERROR_CHECKING
        const bool gl_debug_context = true;
#else
        const bool gl_debug_context = config["gl_debug"].toNumber<bool>();
#endif
        if (gl_debug_context) {
            SDL_GL_SetAttributeErrCheck(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
        }
        // MSAA is handled later with renderbuffers and fbo
        SDL_GL_SetAttributeErrCheck(SDL_GL_MULTISAMPLEBUFFERS, false);
        SDL_GL_SetAttributeErrCheck(SDL_GL_MULTISAMPLESAMPLES, 0);

        LOGI << "Attribute group 1 set" << std::endl;

        // Create window
        const char* window_title = "Overgrowth Online";
        uint32_t window_flags = 0;  // (and SDL_WINDOW_VULKAN or SDL_WINDOW_OPENGL, below)

        int target_monitor = config_.target_monitor();
        if (target_monitor >= config.GetMonitorCount()) {
            target_monitor = config.GetMonitorCount() - 1;
            config.GetRef("target_monitor") = target_monitor;
        }

        currentFullscreenType = config_.full_screen();
        switch (config_.full_screen()) {
            case FullscreenMode::kWindowed: {
                window_flags |= SDL_WINDOW_RESIZABLE;

                window_dims[0] = config_.screen_width();
                window_dims[1] = config_.screen_height();
                break;
            }
            case FullscreenMode::kFullscreen: {
                window_flags |= SDL_WINDOW_FULLSCREEN;

                window_dims[0] = config_.screen_width();
                window_dims[1] = config_.screen_height();
                break;
            }
            case FullscreenMode::kWindowed_borderless: {
                window_flags |= SDL_WINDOW_BORDERLESS;

                window_dims[0] = config_.screen_width();
                window_dims[1] = config_.screen_height();
                break;
            }
            case FullscreenMode::kFullscreen_borderless: {
                window_flags |= SDL_WINDOW_BORDERLESS;

                SDL_DisplayMode displayMode;
                SDL_GetCurrentDisplayMode(target_monitor, &displayMode);

                window_dims[0] = displayMode.w;
                window_dims[1] = displayMode.h;
            }
        }

        SDL_DisplayMode desktop_mode;
        SDL_GetCurrentDisplayMode(target_monitor, &desktop_mode);

        if (window_dims[0] > desktop_mode.w || window_dims[1] > desktop_mode.h) {
            bool fitsW = false;
            bool fitsH = false;

            int displayModeCount = SDL_GetNumDisplayModes(target_monitor);
            for (int i = 0; i < displayModeCount; ++i) {
                SDL_DisplayMode mode;
                SDL_GetDisplayMode(target_monitor, i, &mode);

                if (mode.w >= window_dims[0])
                    fitsW = true;
                if (mode.h >= window_dims[1])
                    fitsH = true;

                if (fitsW && fitsH)
                    break;
            }

            // When DPI scaling is active in windows the window might not fit (says windows)
            // but if it's fullscreen the display settings will be overridden anyway
            if (!fitsW || !fitsH || config_.full_screen() != FullscreenMode::kFullscreen) {
                window_dims[0] = std::min(window_dims[0], desktop_mode.w);
                window_dims[1] = std::min(window_dims[1], desktop_mode.h);

                config.GetRef("screenwidth") = window_dims[0];
                config.GetRef("screenheight") = window_dims[1];

                config_.SetScreenWidth(window_dims[0]);
                config_.SetScreenHeight(window_dims[1]);
            }
        }

        sdl_window_ = NULL;
        if (UseVulkan()) {
            sdl_window_ = SDL_CreateWindow(
                window_title,
                SDL_WINDOWPOS_UNDEFINED_DISPLAY(target_monitor), SDL_WINDOWPOS_UNDEFINED_DISPLAY(target_monitor),
                window_dims[0], window_dims[1],
                window_flags | SDL_WINDOW_VULKAN);
            // ("vk_validation": 1 the validation layer, 2 with synchronization checks; test runs)
            if (sdl_window_ == NULL || !VkBackend::Init(sdl_window_, config_.vSync(), config["vk_validation"].toNumber<int>())) {
                LOGE << "The Vulkan renderer didn't start" << (sdl_window_ ? "" : std::string(" (window: ") + SDL_GetError() + ")") << ": using OpenGL" << std::endl;
                if (sdl_window_) {
                    SDL_DestroyWindow(sdl_window_);
                    sdl_window_ = NULL;
                }
                g_render_backend = RenderBackend::kGL;
            }
        }
        if (!UseVulkan()) {
            if (want_vulkan) {
                deletefile(VulkanStartGuard().c_str());  // (a clean fallback to GL isn't a crash)
            }
            sdl_window_ = SDL_CreateWindow(
                window_title,
                SDL_WINDOWPOS_UNDEFINED_DISPLAY(target_monitor), SDL_WINDOWPOS_UNDEFINED_DISPLAY(target_monitor),
                window_dims[0], window_dims[1],
                window_flags | SDL_WINDOW_OPENGL);
        } else {
            // (the GPU lost or out of reach later: the next start is OpenGL's, stage 5.8)
            VkBackend::OnFatal([] {
                config.GetRef("renderer") = std::string("gl");
                config.Save(GetConfigPath());
            });
        }

        if (!sdl_window_) {
            const char* sdl_error = SDL_GetError();
            LOGE << "Unable to create window, sdl reason: " << sdl_error << std::endl;
            if (strcmp("No matching GL pixel format available", sdl_error) == 0) {
                FatalError("Error", "Could not create window. Make sure your computer is set to 32 bit color depth");
            } else {
                FatalError("Error", "Could not create window");
            }
        }
        LOGI << "Created window" << std::endl;
        SDL_SetWindowMinimumSize(sdl_window_, 640, 480);
        LOGI << "Set minimum size" << std::endl;
        // Set window to be fullscreen (or not)
        SDL_DisplayMode fullscreen_mode;
        SDL_zero(fullscreen_mode);
        fullscreen_mode.format = SDL_PIXELFORMAT_RGB888;
        fullscreen_mode.w = window_dims[0];
        fullscreen_mode.h = window_dims[1];
        SDL_DisplayMode closest_fullscreen_mode;
        SDL_GetClosestDisplayMode(target_monitor, &fullscreen_mode, &closest_fullscreen_mode);
        if (SDL_SetWindowDisplayMode(sdl_window_, &closest_fullscreen_mode) != 0) {
            FatalError("Error", "Could not set window display mode");
        }
        LOGI << "Set display mode" << std::endl;

        if (UseVulkan()) {  // (no GL context)
            GetDrawableSize(sdl_window_, &window_dims[0], &window_dims[1]);
        } else {
            gl_context = SDL_GL_CreateContext(sdl_window_);
            if (gl_context == NULL) {
                LOGE << "OpenGL 4.6 context: " << SDL_GetError() << std::endl;
                FatalError("Error", "Overgrowth Online needs OpenGL 4.6, which this graphics driver doesn't offer. Update the graphics driver (every GPU since about 2012 has OpenGL 4.6), or hold Shift when starting Overgrowth for the normal game.");
            }
            LOGI << "Created OpenGL Context" << std::endl;
            GetDrawableSize(sdl_window_, &window_dims[0], &window_dims[1]);
            // Check what GL context we actually received
            int data[5];
            SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &data[0]);
            SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &data[1]);
            SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &data[2]);
            SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &data[3]);
            SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &data[4]);
            LOGI << "RGBA bits, depth: " << data[0] << " " << data[1] << " " << data[2] << " " << data[3] << " " << data[4] << std::endl;
            SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &data[0]);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &data[1]);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &data[2]);
            if (!data[0]) {
                FatalError("Error", "Could not create double-buffered OpenGL context");
            }
            LOGI << "Anti-aliasing samples: " << data[2] << std::endl;
            int sdl_gl_major, sdl_gl_minor, sdl_gl_profile_mask;
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &sdl_gl_profile_mask);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &sdl_gl_major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &sdl_gl_minor);

            LOGI << "SDL OpenGL Context Result: " << sdl_gl_major << "." << sdl_gl_minor << " With a " << SDL_GLprofile_string((SDL_GLprofile)sdl_gl_profile_mask) << " profile context" << std::endl;

            // Initialize GLAD
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                FatalError("Error", "GLAD error: failed to load OpenGL functions");
            }
            LOGI << "GLAD loaded succesfully" << std::endl;

            GL_PERF_INIT();

            GLint gl_major = 0, gl_minor = 0;
            if (sdl_gl_major >= 3) {
                glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
                glGetIntegerv(GL_MINOR_VERSION, &gl_minor);

                LOGI << "OpenGL Self Reported version string: " << glGetString(GL_VERSION) << std::endl;
                LOGI << "OpenGL Self Reported version values: " << gl_major << "." << gl_minor << std::endl;
                LOGI << "OpenGL renderer: " << glGetString(GL_RENDERER) << ", vendor: " << glGetString(GL_VENDOR)
                     << ", GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
                LogGPUMemory("at start");
            } else if (sdl_gl_major >= 2) {
                LOGI << "OpenGL Self Reported version: " << glGetString(GL_VERSION) << std::endl;
            } else {
                LOGI << "OpenGL has no self reporting routine for 1.x contexts." << std::endl;
            }

            if (GLAD_GL_VERSION_4_6) {
                LOGI << "Context seems acceptable for running the application, continuing." << std::endl;
            } else {
                FatalError("Error", "Overgrowth Online needs OpenGL 4.6, which this graphics driver doesn't offer. Update the graphics driver (every GPU since about 2012 has OpenGL 4.6), or hold Shift when starting Overgrowth for the normal game.");
            }
        }

        DetectAndSetOpenGLFeatureRestrictions();

        g_s3tc_dxt5_textures = config["gl_load_s3tc"].toBool() && g_s3tc_dxt5_support;

        if (g_s3tc_dxt5_textures == false) {
            FatalError("Error", "No support for S3TC DXT5 textures detected. This means either your GPU is too old to run the game, or your drivers are out-of-date");
        }

        if (!UseVulkan()) {  // (Vulkan: the validation layer, "vk_validation")
            // (this was a local of the same name: the setting never reached the callback)
            const bool gl_debug = config["gl_debug"].toNumber<bool>();
            g_opengl_callback_error_dialog = config["opengl_callback_error_dialog"].toNumber<bool>() && !gl_debug;

            if (config["opengl_callback_errors"].toNumber<bool>() || gl_debug) {
                LOGI << "Activating OpenGL callback errors, [opengl_callback_errors]" << std::endl;
                if (GLAD_GL_KHR_debug) {
                    // Overgrowth Online: KHR_debug, with debug output on (it never was)
                    glDebugMessageCallback(&arb_debug_callback, NULL);
                    glEnable(GL_DEBUG_OUTPUT);
                    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                    // A marker of ours, to know the output reaches the callback at all
                    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0x4F4F, GL_DEBUG_SEVERITY_HIGH, -1, "Overgrowth Online: GL debug output check");
                } else if (GLAD_GL_ARB_debug_output) {
                    glDebugMessageCallbackARB(&arb_debug_callback, NULL);
                    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                } else if (GLAD_GL_AMD_debug_output) {
                    glDebugMessageEnableAMD(0, 0, 0, NULL, true);
                    glDebugMessageCallbackAMD(&amd_debug_callback, NULL);
                    // GL_DEBUG_OUTPUT_SYNCHRONOUS is part of the KHR and ARB
                    // extension, but not the AMD one
                    // glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                }
            } else {
                LOGI << "Deactivating OpenGL callback errors, [opengl_callback_errors]" << std::endl;
            }
        }

        FrameTimers::Init();  // Overgrowth Online: "frame_timers", "gl_debug_groups"

        if (UseVulkan()) {  // (Vulkan 1.3 has these; vsync and reversed-Z's [0, 1] depth are the backend's)
            features_.SetFrameBufferFSAAEnabled(true);
            g_attrib_envobj_instancing_support = true;
        } else {
            while (glGetError() != GL_NO_ERROR) {
                // Throw away GL errors from SDL/GLEW init
            }
            CHECK_GL_ERROR();
            // Determine what framebuffer features are supported
            if (GLAD_GL_VERSION_3_0 || GLAD_GL_EXT_framebuffer_object || GLAD_GL_ARB_framebuffer_object) {
            } else {
                FatalError("Error", "No support for Framebuffer Objects detected");
            }
            features_.SetFrameBufferFSAAEnabled(GLAD_GL_VERSION_3_0 || GLAD_GL_EXT_framebuffer_multisample != 0);
            CHECK_GL_ERROR();
            // Determine what instanced array features are supported
            if (GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_instanced_arrays) {
                g_attrib_envobj_instancing_support = true;
            } else {
                g_attrib_envobj_instancing_support = false;
            }
            ApplyVsync(config_.vSync());
            // Overgrowth Online (GL 4.6 stage 4a): reversed-Z in Vulkan's [0, 1] clip range,
            // depth 1 at the near plane, cleared to 0 (far), tested GREATER (see reversez.h)
            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
            glClearDepth(0.0);
            glDepthFunc(GL_GREATER);
            // Clear screen
            SetClearColor(vec4(0.5f, 0.5f, 0.5f, 0.0f));
            setViewport(0, 0, window_dims[0], window_dims[1]);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            SDL_GL_SwapWindow(sdl_window_);
            CHECK_GL_ERROR();
        }
        PROFILER_LEAVE(g_profiler_ctx);

        textures->ResetVRAM();
        shaders->ResetVRAM();
        CHECK_GL_ERROR();

        vendor = GetGLVendor();
        SetAnisotropy(config_.anisotropy());

        features_.SetHDREnable(UseVulkan() || GLAD_GL_VERSION_3_0 || GLAD_GL_ARB_texture_float);

        // Set initial GL state so it is in sync with shadow state
        if (!UseVulkan()) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        shadow_state_.gl_state_.depth_test = false;
        shadow_state_.gl_state_.cull_face = false;
        shadow_state_.gl_state_.blend = false;
        shadow_state_.gl_state_.blend_src = GL_SRC_ALPHA;
        shadow_state_.gl_state_.blend_dst = GL_ONE_MINUS_SRC_ALPHA;
        shadow_state_.gl_state_.blend_alpha_src = GL_ONE;
        shadow_state_.gl_state_.blend_alpha_dst = GL_ONE_MINUS_SRC_ALPHA;
        shadow_state_.polygon_offset_enable_ = false;

        current_fbo_ = 0;
        current_target_ = RenderTarget();
        current_draw_count_ = -1;

        drawing_shadow = false;

        line_width = -1;

// Warn if not enough shader varyings are supported
#ifndef __APPLE__  // Why does this not work on Mac OS 10.7.5 core profile?
        int assumed_floats = 32;
        int max_varying_floats;
        GLVK(GetIntegerv)(GL_MAX_VARYING_FLOATS, &max_varying_floats);
        if (max_varying_floats < assumed_floats) {
            std::ostringstream oss;
            oss << "Only " << max_varying_floats << " varying floats supported, "
                << "up to " << assumed_floats << " are used in shaders.";
            DisplayError("Warning", oss.str().c_str());
        }
#endif

        textures->setWrap(GL_CLAMP_TO_EDGE);
        CHECK_GL_ERROR();

        int w = config_.screen_width();
        int h = config_.screen_height();
        SetUpWindowDims(w, h);

        PROFILER_ENTER(g_profiler_ctx, "Initializing decaltextures");
        DecalTextures::Instance()->Init();
        PROFILER_LEAVE(g_profiler_ctx);

        if (GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_tessellation_shader) {
            glPatchParameteri(GL_PATCH_VERTICES, 3);
        }
    }

    last_shadow_update_time = 0.0f;

    use_sample_alpha_to_coverage = true;
    if (config_.FSAA_samples() < 2) {
        use_sample_alpha_to_coverage = false;
    }

    if (render_dims[0] != old_render_dims[0] || render_dims[1] != old_render_dims[1] || config_.FSAA_samples() != old_FSAA_samples) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "Setting up framebuffers for post effects");
        PushRenderTarget();

        // (Overgrowth Online stage 5.0: render targets of textures; MSAA as multisample
        // textures, resolved into the screen textures)
        if (multisample_framebuffer_exists) {
            multisample_target = RenderTarget();
            multisample_color_tex.clear();
            multisample_vel_tex.clear();
            multisample_depth_tex.clear();
            multisample_framebuffer_exists = false;
        }
        if (features_.frame_buffer_fsaa_enabled() && config_.FSAA_samples() > 1) {
            int samples = config_.FSAA_samples();
            GLint color_format = features_.HDR_enable() ? GL_RGBA16F : GL_RGBA8;
            multisample_color_tex = textures->makeMultisampleTexture(render_dims[0], render_dims[1], samples, color_format);
            textures->SetTextureName(multisample_color_tex, "Post::Multisample Color");
            multisample_vel_tex = textures->makeMultisampleTexture(render_dims[0], render_dims[1], samples, color_format);
            textures->SetTextureName(multisample_vel_tex, "Post::Multisample Velocity");
            multisample_depth_tex = textures->makeMultisampleTexture(render_dims[0], render_dims[1], samples, GL_DEPTH_COMPONENT32F);  // (float: reversed-Z)
            textures->SetTextureName(multisample_depth_tex, "Post::Multisample Depth");
            multisample_target = RenderTarget().Color(multisample_color_tex).Color(multisample_vel_tex).Depth(multisample_depth_tex);
            BindRenderTarget(multisample_target);
            CHECK_GL_ERROR();
            multisample_framebuffer_exists = true;
        }
        if (framebuffer_exists) {
            scene_target = RenderTarget();
            screen_color_tex.clear();
            screen_vel_tex.clear();
            screen_depth_tex.clear();
            post_effects.temp_screen_tex.clear();
            post_effects.tone_mapped_tex.clear();
        }
        textures->DeleteUnusedTextures();

        screen_color_tex = textures->makeRectangularTexture(render_dims[0], render_dims[1], features_.HDR_enable() ? GL_RGBA16F : GL_RGBA, GL_RGBA);
        textures->SetTextureName(screen_color_tex, "Post::Screen Color");
        screen_vel_tex = textures->makeRectangularTexture(render_dims[0], render_dims[1], features_.HDR_enable() ? GL_RGBA16F : GL_RGBA, GL_RGBA);
        textures->SetTextureName(screen_vel_tex, "Post::Screen Velocity");
        screen_depth_tex = textures->makeRectangularTexture(render_dims[0], render_dims[1], GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT);  // (float: reversed-Z)
        textures->SetTextureName(screen_depth_tex, "Post::Screen Depth");

        post_effects.temp_screen_tex = textures->makeRectangularTexture(
            render_dims[0], render_dims[1],
            features().HDR_enable() ? GL_RGBA16F : GL_RGBA, GL_RGBA);
        textures->SetTextureName(post_effects.temp_screen_tex, "Post::Screen Color - Temp");

        post_effects.tone_mapped_tex = textures->makeRectangularTexture(
            render_dims[0], render_dims[1],
            features().HDR_enable() ? GL_RGBA16F : GL_RGBA, GL_RGBA);
        textures->SetTextureName(post_effects.tone_mapped_tex, "Post::Tone-Mapped");

        scene_target = RenderTarget().Color(screen_color_tex).Color(screen_vel_tex).Depth(screen_depth_tex);
        BindRenderTarget(scene_target);
        CHECK_FBO_ERROR();
        framebuffer_exists = true;

        PopRenderTarget();
    }

    SetUpShadowTextures();

    if (first_load) {
        textures->setWrap(GL_REPEAT);
        noise_ref = Engine::Instance()->GetAssetManager()->LoadSync<TextureAsset>("Data/Textures/noise.tga", PX_NOREDUCE | PX_NOCONVERT, 0x0);
        pure_noise_ref = Engine::Instance()->GetAssetManager()->LoadSync<TextureAsset>("Data/Textures/purenoise_nocompress.tga", PX_NOREDUCE | PX_NOCONVERT, 0x0);

        GLVK(GetIntegerv)(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_texture_units);
        LOGI << max_texture_units << " texture units supported." << std::endl;
        if (max_texture_units < 16) {
            DisplayError("Warning", "Fewer than 16 texture units supported!");
        }

#ifdef __APPLE__
        // Mac multithreaded OpenGL?
        /*CGLContextObj cctx = CGLGetCurrentContext();
        CGLError err = CGLEnable( cctx, kCGLCEMPEngine);
        if(err != kCGLNoError){
            DisplayError("Error","Problem initializing multi-threaded OpenGL");
        }*/
#endif

        GLint param;
        GLVK(GetIntegerv)(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &param);
        LOGI << "GL_MAX_VERTEX_UNIFORM_COMPONENTS: " << param << std::endl;
    }

    GLVK(GetIntegerv)(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attrib_arrays);
    old_render_dims[0] = render_dims[0];
    old_render_dims[1] = render_dims[1];
    old_FSAA_samples = config_.FSAA_samples();
    first_load = false;
    gl_lib_loaded_ = true;
    settings_changed = true;
    SetSeamlessCubemaps(config_.seamless_cubemaps_);

    if (!UseVulkan()) {  // (Vulkan: vertex layouts, stage 5.5)
        if (!glGenVertexArrays) {
            FatalError("Error", "No support detected for vertex array objects");
        }
        // Needed for a core context. Overgrowth Online: one per context; a new one
        // has every attribute off, so the cache starts over with it
        if (vao_ == 0 || vao_context_ != gl_context) {
            glGenVertexArrays(1, &vao_);
            vao_context_ = gl_context;
            active_vertex_attrib_arrays = 0;
            wanted_vertex_attrib_arrays = 0;
        }
        glBindVertexArray(vao_);
        CHECK_GL_ERROR();

        if (GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_texture_cube_map_array) {
            LOGI << "GLAD_ARB_texture_cube_map_array is available" << std::endl;
        } else {
            LOGI << "GLAD_ARB_texture_cube_map_array is NOT available" << std::endl;
        }

        if (!GLAD_GL_EXT_texture_compression_s3tc) {
            LOGF << "Missing necessary GL extension: EXT_texture_compression_s3tc" << std::endl;
        }

        if (!GLAD_GL_EXT_texture_sRGB) {
            LOGF << "Missing necessary GL extension: EXT_texture_sRGB" << std::endl;
        }
    }

    // Assume that the bindings in the state aren't valid anymore.
    // There have been indications in the NVIDIA driver on linux that this is
    // the case.
    LOGI << "Unassuming that any EBO or VBO is bound" << std::endl;
    vbo_element_bound = -1;
    vbo_array_bound = -1;
}

void Graphics::SetModelMatrix(const mat4& matrix, const mat3& normal_matrix) {
    Shaders::Instance()->GetCurrentProgram()->SetUniform(MODEL_MATRIX, matrix);
    Shaders::Instance()->GetCurrentProgram()->SetUniform(NORMAL_MATRIX, normal_matrix);
}

struct SimpleLineDrawState {
    GLState state;
    SimpleLineDrawState() {
        state.blend = true;
        state.cull_face = false;
        state.depth_test = true;
        state.depth_write = false;
    }
};

static SimpleLineDrawState simple_line_draw_state;

void Graphics::SetSimpleLineDrawState() {
    setGLState(simple_line_draw_state.state);
    Shaders::Instance()->noProgram();
}

void Graphics::BlitColorBuffer() {
    // (Overgrowth Online stage 5.0: colour and velocity, through BlitRenderTarget)
    PixelRect all(0, 0, render_dims[0], render_dims[1]);
    BlitRenderTarget(multisample_target, 0, all, scene_target, 0, all);
    BlitRenderTarget(multisample_target, 1, all, scene_target, 1, all);
}

void Graphics::SetFromConfig(const Config& config, bool dynamic) {
    if (dynamic && config_.full_screen() == FullscreenMode::kWindowed) {
        config_.SetScreenWidth(config["screenwidth"].toNumber<int>());
        config_.SetScreenHeight(config["screenheight"].toNumber<int>());
    }
    if (!dynamic) {
        config_.SetScreenWidth(config["screenwidth"].toNumber<int>());
        config_.SetScreenHeight(config["screenheight"].toNumber<int>());
        config_.SetFullscreen(static_cast<FullscreenMode::Mode>(config["fullscreen"].toNumber<int>()));
        config_.SetTextureReduce(config["texture_reduce"].toNumber<int>());
        config_.SetFSAASamples(config["multisample"].toNumber<int>());
        config_.SetAnisotropy(config["anisotropy"].toNumber<float>());
        config_.SetVSync(config["vsync"].toNumber<bool>());
        config_.SetLimitFpsInGame(config["limit_fps_in_game"].toNumber<bool>());
        config_.SetMaxFrameRate(config["max_frame_rate"].toNumber<int>());
        config_.motion_blur_amount_ = config["motion_blur_amount"].toNumber<float>();
        config_.SetSimpleFog(config["simple_fog"].toNumber<bool>());
        config_.SetDetailObjects(config["detail_objects"].toNumber<bool>());
        config_.SetDetailObjectDecals(config["detail_object_decals"].toNumber<bool>());
        config_.SetDetailObjectLowres(config["detail_object_lowres"].toNumber<bool>());
        config_.SetDetailObjectShadows(!config["detail_object_disable_shadows"].toNumber<bool>());
        config_.SetDepthOfField(config["depth_of_field"].toNumber<bool>());
        config_.SetDepthOfFieldReduced(config["depth_of_field_reduced"].toNumber<bool>());
    }
    config_.SetBlood(config["blood"].toNumber<int>());
    config_.SetBloodColor(GraphicsConfig::BloodColorFromString(config["blood_color"].str()));
    config_.SetSplitScreen(config["split_screen"].toNumber<bool>());
    config_.seamless_cubemaps_ = config["seamless_cubemaps"].toNumber<bool>();
    setSimpleShadows(config["simple_shadows"].toNumber<bool>());
    setSimpleWater(config["simple_water"].toNumber<bool>());
    SetParticleFieldSimple(config["particle_field_simple"].toNumber<bool>());
    SetDetailObjectsReduced(config["detail_objects_reduced"].toNumber<bool>());
    g_attrib_envobj_instancing_enabled = config["attrib_envobj_instancing"].toNumber<bool>();
    g_perform_occlusion_query = config["occlusion_query"].toNumber<bool>();
    g_gamma_correct_final_output = config["gamma_correct_final_output"].toNumber<bool>();

    config_.SetTargetMonitor(config["target_monitor"].toNumber<int>());
}

void Graphics::SetWindowGrab(bool val) {
    if (!val || SDL_GetKeyboardFocus() == sdl_window_) {
        SDL_SetWindowGrab(sdl_window_, val ? SDL_TRUE : SDL_FALSE);
    }
}

void Graphics::SetLineWidth(int val) {
    if (line_width != val) {
        line_width = val;
#ifndef PLATFORM_MACOSX
        if (!UseVulkan()) {  // (Vulkan: the draw state's)
            glLineWidth((float)val);
        }
#endif
    }
}

void Graphics::LogGPUMemory(const char* when) {
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        LOGI << "GPU memory " << when << ": " << VkBackend::MemoryDescription() << std::endl;
        return;
    }
    if (GLAD_GL_NVX_gpu_memory_info) {
        GLint dedicated = 0, available = 0, evictions = 0, evicted = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &available);
        glGetIntegerv(GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX, &evictions);
        glGetIntegerv(GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX, &evicted);
        LOGI << "GPU memory " << when << ": " << available / 1024 << " of " << dedicated / 1024 << " MB free, "
             << evictions << " evictions (" << evicted / 1024 << " MB)" << std::endl;
    } else if (GLAD_GL_ATI_meminfo) {
        GLint texture[4] = {0, 0, 0, 0}, vbo[4] = {0, 0, 0, 0};  // free KB: pool, largest block, shared, largest shared
        glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, texture);
        glGetIntegerv(GL_VBO_FREE_MEMORY_ATI, vbo);
        LOGI << "GPU memory " << when << ": textures " << texture[0] / 1024 << " MB free (largest block " << texture[1] / 1024
             << ", shared " << texture[2] / 1024 << "), buffers " << vbo[0] / 1024 << " MB free" << std::endl;
    }
}

void Graphics::ResizeWindow(int& w, int& h) {
    if (config_.full_screen() == FullscreenMode::kFullscreen) {
        SDL_DisplayMode fullscreen_mode;
        SDL_zero(fullscreen_mode);
        fullscreen_mode.format = SDL_PIXELFORMAT_RGB888;
        fullscreen_mode.w = w;
        fullscreen_mode.h = h;
        SDL_DisplayMode closest_fullscreen_mode;
        if (SDL_GetClosestDisplayMode(config_.target_monitor(), &fullscreen_mode, &closest_fullscreen_mode) == NULL) {
            LOGF << SDL_GetError() << std::endl;
        }
        if (SDL_SetWindowDisplayMode(sdl_window_, &closest_fullscreen_mode) != 0)
            LOGF << SDL_GetError() << std::endl;

        SDL_SetWindowSize(sdl_window_, closest_fullscreen_mode.w, closest_fullscreen_mode.h);

        w = closest_fullscreen_mode.w;
        h = closest_fullscreen_mode.h;
    } else
        SDL_SetWindowSize(sdl_window_, w, h);

    if (config_.full_screen() == FullscreenMode::kFullscreen_borderless || config_.full_screen() == FullscreenMode::kWindowed_borderless) {
        int targetMonitor = config_.target_monitor();
        SDL_SetWindowPosition(sdl_window_, SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor), SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor));
    }

    GetDrawableSize(sdl_window_, &window_dims[0], &window_dims[1]);
}

void Graphics::SetUpWindowDims(int& w, int& h) {
    switch (config_.full_screen()) {
        case FullscreenMode::kWindowed:
        case FullscreenMode::kWindowed_borderless:
            ResizeWindow(w, h);
            window_dims[0] = w;
            window_dims[1] = h;
            render_dims[0] = window_dims[0];
            render_dims[1] = window_dims[1];
            render_output_dims[0] = window_dims[0];
            render_output_dims[1] = window_dims[1];
            break;
        case FullscreenMode::kFullscreen:
            ResizeWindow(w, h);
            SDL_GetWindowSize(sdl_window_, &render_dims[0], &render_dims[1]);
            SDL_GetWindowSize(sdl_window_, &render_output_dims[0], &render_output_dims[1]);
            break;
        case FullscreenMode::kFullscreen_borderless:
            render_dims[0] = w;
            render_dims[1] = h;
            render_output_dims[0] = window_dims[0];
            render_output_dims[1] = window_dims[1];
            if (render_output_dims[0] > render_output_dims[1] * render_dims[0] / render_dims[1]) {
                render_output_dims[0] = render_output_dims[1] * render_dims[0] / render_dims[1];
            } else if (render_output_dims[0] < render_output_dims[1] * render_dims[0] / render_dims[1]) {
                render_output_dims[1] = render_output_dims[0] * render_dims[1] / render_dims[0];
            }
            break;
    }

    config_.SetScreenWidth(w);
    config_.SetScreenHeight(h);
}

void Graphics::SetResolution(int w, int h, bool force) {
    LOGI << "SetResolution has been called" << std::endl;
    if (render_dims[0] == w && render_dims[1] == h && !force) {
        return;
    }
    SetUpWindowDims(w, h);
    if (initialized) {
        InitScreen();
    }
    LOGI << "Updating the screen resolution in the config file." << std::endl;
    config.GetRef("screenwidth") = w;
    config.GetRef("screenheight") = h;
    if (initialized) {
        Engine::Instance()->InjectWindowResizeEvent(ivec2(w, h));
    }
}

void Graphics::SetTargetMonitor(int targetMonitor) {
    if (SDL_GetWindowDisplayIndex(sdl_window_) != targetMonitor) {
        if (currentFullscreenType == FullscreenMode::kFullscreen) {
            SDL_SetWindowFullscreen(sdl_window_, SDL_FALSE);
            SDL_SetWindowPosition(sdl_window_, SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor), SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor));
            SDL_SetWindowFullscreen(sdl_window_, SDL_TRUE);
        } else
            SDL_SetWindowPosition(sdl_window_, SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor), SDL_WINDOWPOS_CENTERED_DISPLAY(targetMonitor));

        SDL_DisplayMode displayMode;
        SDL_GetDesktopDisplayMode(config_.target_monitor(), &displayMode);

        if (window_dims[0] > displayMode.w || window_dims[1] > displayMode.h)
            SetResolution(displayMode.w, displayMode.h, true);
    }
}

void Graphics::CheckForWindowResize() {
    int test[2];
    GetDrawableSize(sdl_window_, &test[0], &test[1]);
    if (test[0] != window_dims[0] || test[1] != window_dims[1]) {
        SetResolution(test[0], test[1], true);
    }
}

void Graphics::SetFullscreen(FullscreenMode::Mode val) {
    config_.SetFullscreen(val);

    // If entering windowed mode, resize the window to half the display resolution and center it,
    // Otherwise the title bar (on windows at least) might appear outside of the screen bounds
    bool resize = false;
    bool recenter = false;

    switch (val) {
        case FullscreenMode::kWindowed: {
            if (currentFullscreenType != FullscreenMode::kWindowed) {
                resize = true;
                recenter = true;
            }

            SDL_SetWindowFullscreen(sdl_window_, SDL_FALSE);
            SDL_SetWindowBordered(sdl_window_, SDL_TRUE);
#if PLATFORM_WINDOWS  // Following function added in SDL 2.0.5
            SDL_SetWindowResizable(sdl_window_, SDL_TRUE);
#endif
            break;
        }
        case FullscreenMode::kFullscreen: {
            int w = config_.screen_width();
            int h = config_.screen_height();
            ResizeWindow(w, h);

            SDL_SetWindowFullscreen(sdl_window_, SDL_WINDOW_FULLSCREEN);
            break;
        }
        case FullscreenMode::kWindowed_borderless: {
            SDL_SetWindowFullscreen(sdl_window_, SDL_WINDOW_BORDERLESS);
            SDL_SetWindowBordered(sdl_window_, SDL_FALSE);
#if PLATFORM_WINDOWS  // Following function added in SDL 2.0.5
            SDL_SetWindowResizable(sdl_window_, SDL_FALSE);
#endif
            recenter = true;
            break;
        }
        case FullscreenMode::kFullscreen_borderless: {
            SDL_SetWindowFullscreen(sdl_window_, SDL_WINDOW_BORDERLESS);
            SDL_SetWindowBordered(sdl_window_, SDL_FALSE);
#if PLATFORM_WINDOWS  // Following function added in SDL 2.0.5
            SDL_SetWindowResizable(sdl_window_, SDL_FALSE);
#endif

            SDL_DisplayMode displayMode;
            SDL_GetDesktopDisplayMode(config_.target_monitor(), &displayMode);

            ResizeWindow(displayMode.w, displayMode.h);  // Recenters window
            break;
        }
    }

    if (resize) {
        SDL_DisplayMode displayMode;
        SDL_GetDesktopDisplayMode(config_.target_monitor(), &displayMode);
        SetResolution(displayMode.w / 2, displayMode.h / 2, true);
    } else {
        SetResolution(render_dims[0], render_dims[1], true);
    }

    if (recenter) {
        SDL_SetWindowPosition(sdl_window_, SDL_WINDOWPOS_CENTERED_DISPLAY(config_.target_monitor()), SDL_WINDOWPOS_CENTERED_DISPLAY(config_.target_monitor()));
    }

    GetDrawableSize(sdl_window_, &window_dims[0], &window_dims[1]);
    currentFullscreenType = val;
}

void Graphics::SetFSAA(int val) {
    if (config_.FSAA_samples() != val) {
        config_.SetFSAASamples(val);
        if (initialized) {
            InitScreen();
        }
    }
}

void Graphics::SetVsync(bool val) {
    config_.SetVSync(val);
    ApplyVsync(val);
}

void Graphics::SetLimitFpsInGame(bool val) {
    config_.SetLimitFpsInGame(val);
}

void Graphics::SetMaxFrameRate(int val) {
    config_.SetMaxFrameRate(val);
}

void Graphics::SetSimpleFog(bool val) {
    config_.SetSimpleFog(val);
}

void Graphics::SetDepthOfField(bool val) {
    config_.SetDepthOfField(val);
}

void Graphics::SetDepthOfFieldReduced(bool val) {
    config_.SetDepthOfFieldReduced(val);
}

void Graphics::SetDetailObjects(bool val) {
    config_.SetDetailObjects(val);
}

void Graphics::SetDetailObjectDecals(bool val) {
    config_.SetDetailObjectDecals(val);
}

void Graphics::SetDetailObjectLowres(bool val) {
    config_.SetDetailObjectLowres(val);
}

void Graphics::SetDetailObjectsReduced(bool val) {
    g_detail_objects_reduced = val;
    g_detail_objects_reduced_dirty = true;
}

void Graphics::SetDetailObjectShadows(bool val) {
    config_.SetDetailObjectShadows(val);
}

void Graphics::SetSeamlessCubemaps(bool val) {
    if (UseVulkan()) {  // (Vulkan: cube maps are always seamless)
        config_.seamless_cubemaps_ = val;
        return;
    }
    if (GLAD_GL_VERSION_3_2 || GLAD_GL_ARB_seamless_cube_map || GLAD_GL_ARB_seamless_cubemap_per_texture) {
        if (val) {
            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        } else {
            glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        }
        config_.seamless_cubemaps_ = val;
    }
}

// Overgrowth Online (GL 4.6 stage 5.4): GL's state for a Vulkan draw
static int ReversedDepthFunc(const GraphicsShadowState& s) {
    if (s.depth_func_unset_) {
        return GL_GREATER;  // (InitScreen's glDepthFunc)
    }
    switch (s.depth_func_type_) {
        case GL_LESS: return GL_GREATER;
        case GL_LEQUAL: return GL_GEQUAL;
        case GL_GREATER: return GL_LESS;
        case GL_GEQUAL: return GL_LEQUAL;
        default: return s.depth_func_type_;
    }
}

void Graphics::VkDrawCall(GLenum mode, unsigned int first, unsigned int count, bool indexed, GLenum type, const void* indices, unsigned int instances) {
    VkDraw::State st;
    const GLState& g = shadow_state_.gl_state_;
    st.blend = g.blend;
    st.blend_src = g.blend_src;
    st.blend_dst = g.blend_dst;
    st.blend_alpha_src = g.blend_alpha_src;
    st.blend_alpha_dst = g.blend_alpha_dst;
    st.depth_test = g.depth_test;
    st.depth_write = g.depth_write;
    st.depth_func = ReversedDepthFunc(shadow_state_);
    st.cull = g.cull_face;
    st.polygon_offset = shadow_state_.polygon_offset_enable_;
    st.alpha_to_coverage = shadow_state_.alpha_to_coverage_;
    st.color_mask = shadow_state_.color_mask_;
    st.wireframe = wireframe_;
    st.line_width = line_width > 0 ? (float)line_width : 1.0f;
    st.scissor_test = shadow_state_.scissor_test_;
    for (int i = 0; i < 4; ++i) {
        st.scissor[i] = gl_scissor_[i];
        st.viewport[i] = gl_viewport_[i];
    }
    Shaders* shaders = Shaders::Instance();
    const GLuint program = shaders->bound_program >= 0 ? shaders->programs[shaders->bound_program].gl_program : 0;
    VkDraw::Draw(st, program, mode, first, count, indexed, type, (size_t)(uintptr_t)indices, instances);
}

// (diagnostics, both renderers: config debug_skip_program skips the draws of programs
// whose name has it; debug_log_programs logs each program's first draw)
// (diagnostics, both renderers: config debug_frame_log logs each frame much slower than
// the last 120's median; on Vulkan, where it waited)
static void LogSlowFrame() {
    static const bool on = config["debug_frame_log"].toNumber<bool>();
    if (!on) {
        return;
    }
    static std::chrono::steady_clock::time_point last;
    static bool have_last = false;
    static std::deque<double> times;
    static uint64_t frames = 0, last_pipelines = 0;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    ++frames;
    uint64_t pipelines = 0;
    if (UseVulkan()) {
        pipelines = VkDraw::GetStats().pipelines;
    }
    if (have_last) {
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        if (times.size() >= 60) {
            std::vector<double> sorted(times.begin(), times.end());
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            const double median = sorted[sorted.size() / 2];
            if (ms > std::max(median * 1.6, median + 2.0)) {
                std::ostringstream oss;
                oss << "Slow frame " << frames << ": " << ms << " ms (median " << median << ")";
                if (UseVulkan()) {
                    const VkBackend::FrameDiag d = VkBackend::LastFrameDiag();
                    oss << ": slot wait " << d.slot_wait_ms << " ms, submits " << d.submits << ", GPU waits " << d.waits << " (" << d.wait_ms
                        << " ms), pipelines +" << (pipelines - last_pipelines);
                }
                LOGI << oss.str() << std::endl;
            }
        }
        times.push_back(ms);
        if (times.size() > 120) {
            times.pop_front();
        }
    }
    last = now;
    have_last = true;
    last_pipelines = pipelines;
}

static struct {
    std::vector<std::pair<std::string, uint32_t> > todo;
    bool listed = false;
    size_t next = 0;
} g_menu_warm;

void Graphics::VulkanMenuWarmUp() {
    if (!UseVulkan()) {
        return;
    }
    std::vector<std::pair<std::string, uint32_t> >& todo = g_menu_warm.todo;
    size_t& next = g_menu_warm.next;
    if (!g_menu_warm.listed) {
        g_menu_warm.listed = true;
        todo = VkDraw::WarmUpPrograms();
    }
    if (next < todo.size()) {
        // (about 2 ms of each frame: making a program reads and reflects its SPIR-V)
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        Shaders* shaders = Shaders::Instance();
        while (next < todo.size() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2)) {
            const std::pair<std::string, uint32_t>& p = todo[next++];
            if (shaders->ProgramSourcesExist(p.first)) {
                shaders->createProgram(shaders->returnProgram(p.first, (Shaders::OptionalShaders)p.second));
            }
        }
        return;
    }
    VkDraw::StartWarmUp();  // (once; again after a level's load stopped it part way)
    VkDraw::DrainWarmUp();
}

bool Graphics::VulkanWarmUpProgress(int* done, int* total, bool* pipelines) const {
    if (!UseVulkan() || !g_menu_warm.listed) {
        return false;
    }
    if (g_menu_warm.next < g_menu_warm.todo.size()) {
        *done = (int)g_menu_warm.next;
        *total = (int)g_menu_warm.todo.size();
        *pipelines = false;
        return true;
    }
    size_t d = 0, t = 0;
    if (!VkDraw::WarmUpProgress(&d, &t)) {
        return false;
    }
    *done = (int)d;
    *total = (int)t;
    *pipelines = true;
    return true;
}

// Overgrowth Online (GL 4.6 stage 5.8): there from a Vulkan start until its first frames
std::string VulkanStartGuard() {
    return GetWritePath(CoreGameModID) + "vulkan_starting.txt";
}

static bool SkipDrawForDebug() {
    static const std::string skip = config["debug_skip_program"].str();
    static const bool log_programs = config["debug_log_programs"].toNumber<bool>();
    if (skip.empty() && !log_programs) {
        return false;
    }
    Shaders* shaders = Shaders::Instance();
    if (shaders->bound_program < 0) {
        return false;
    }
    const std::string& name = shaders->programs[shaders->bound_program].name;
    if (log_programs) {
        static std::set<std::string> logged;
        if (logged.insert(name).second) {
            LOGI << "Draw program: " << name << std::endl;
        }
    }
    return !skip.empty() && name.find(skip) != std::string::npos;
}

void Graphics::DrawArrays(GLenum mode, int first, unsigned int count) {
    if (SkipDrawForDebug()) {
        return;
    }
    Shaders::Instance()->FlushShadowBlock();  // Overgrowth Online (GL 4.6 stage 3.4)
    SetVertexAttribArrays();
    if (UseVulkan()) {
        VkDrawCall(mode, (unsigned)first, count, false, GL_NONE, NULL, 1);
        return;
    }
    GL_PERF_START();
    glDrawArrays(mode, first, count);
    GL_PERF_END();
}

void Graphics::DrawElements(GLenum mode, unsigned int count, GLenum type, const void* indices) {
    if (SkipDrawForDebug()) {
        return;
    }
    Shaders::Instance()->FlushShadowBlock();  // Overgrowth Online (GL 4.6 stage 3.4)
    SetVertexAttribArrays();
    if (UseVulkan()) {
        VkDrawCall(mode, 0, count, true, type, indices, 1);
        return;
    }
    GL_PERF_START();
    glDrawElements(mode, count, type, indices);
    GL_PERF_END();
}

void Graphics::DrawElementsInstanced(GLenum mode, unsigned int count, GLenum type, const void* indices, unsigned int primcount) {
    if (SkipDrawForDebug()) {
        return;
    }
    Shaders::Instance()->FlushShadowBlock();  // Overgrowth Online (GL 4.6 stage 3.4)
    SetVertexAttribArrays();
    if (UseVulkan()) {
        VkDrawCall(mode, 0, count, true, type, indices, primcount);
        return;
    }
    GL_PERF_START();
    glDrawElementsInstanced(mode, count, type, indices, primcount);
    GL_PERF_END();
}

void Graphics::DrawRangeElements(GLenum mode, unsigned int start, unsigned int end, unsigned int count, GLenum type, const void* indices) {
    if (SkipDrawForDebug()) {
        return;
    }
    Shaders::Instance()->FlushShadowBlock();  // Overgrowth Online (GL 4.6 stage 3.4)
    SetVertexAttribArrays();
    if (UseVulkan()) {
        VkDrawCall(mode, 0, count, true, type, indices, 1);
        return;
    }
    GL_PERF_START();
    glDrawRangeElements(mode, start, end, count, type, indices);
    GL_PERF_END();
}

void VertexLayout::Add(const char* name, GLuint binding, GLint size, GLenum type, GLboolean normalized, GLuint offset) {
    Attrib a;
    a.name = name;
    a.binding = binding;
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.offset = offset;
    attribs.push_back(a);
    if (divisors.size() <= binding) {
        divisors.resize(binding + 1, 0);
    }
}

void VertexLayout::SetDivisor(GLuint binding, GLuint divisor) {
    if (divisors.size() <= binding) {
        divisors.resize(binding + 1, 0);
    }
    divisors[binding] = divisor;
}

void Graphics::BeginVertexLayout(VertexLayout& layout, int program, uint32_t enabled, const GLuint* buffers, const GLintptr* offsets, const GLsizei* strides, GLuint element_buffer) {
    // The VAO for this program's input locations (one per distinct set)
    int index = -1;
    for (const std::pair<int, int>& pv : layout.program_vao) {
        if (pv.first == program) {
            index = pv.second;
            break;
        }
    }
    if (index == -1) {
        Shaders* shaders = Shaders::Instance();
        std::vector<GLint> locations(layout.attribs.size());
        for (size_t i = 0; i < layout.attribs.size(); ++i) {
            locations[i] = shaders->returnShaderAttrib(layout.attribs[i].name, program);
        }
        for (size_t v = 0; v < layout.vaos.size(); ++v) {
            if (layout.vaos[v].locations == locations) {
                index = (int)v;
                break;
            }
        }
        if (index == -1) {
            VertexLayout::Vao v;
            if (UseVulkan()) {  // (Vulkan: stage 5.5)
                v.vao = VkStub::NewId();
            } else {
                glCreateVertexArrays(1, &v.vao);
                for (size_t i = 0; i < layout.attribs.size(); ++i) {
                    if (locations[i] == -1) {
                        continue;
                    }
                    const VertexLayout::Attrib& a = layout.attribs[i];
                    glVertexArrayAttribFormat(v.vao, locations[i], a.size, a.type, a.normalized, a.offset);
                    glVertexArrayAttribBinding(v.vao, locations[i], a.binding);
                }
                for (size_t b = 0; b < layout.divisors.size(); ++b) {
                    glVertexArrayBindingDivisor(v.vao, (GLuint)b, layout.divisors[b]);
                }
            }
            v.locations = locations;
            v.enabled = 0;
            layout.vaos.push_back(v);
            index = (int)layout.vaos.size() - 1;
        }
        layout.program_vao.push_back(std::make_pair(program, index));
    }
    VertexLayout::Vao& v = layout.vaos[index];
    g_layout_label = layout.attribs.empty() ? "" : layout.attribs.back().name.c_str();
    if (!vertex_layout_bound_ || layout_vao_bound_ != v.vao) {
        if (!UseVulkan()) {  // (Vulkan: stage 5.5)
            glBindVertexArray(v.vao);
        }
        layout_vao_bound_ = v.vao;
    }
    vertex_layout_bound_ = true;

    uint32_t on = 0;
    for (size_t i = 0; i < layout.attribs.size(); ++i) {
        if ((enabled & (1u << i)) && v.locations[i] != -1) {
            on |= 1u << i;
            if (!UseVulkan()) {  // (Vulkan: stage 5.5)
                glVertexArrayVertexBuffer(v.vao, layout.attribs[i].binding, buffers[i], offsets[i], strides[i]);
            }
        }
    }
    if (UseVulkan()) {
        // (the attributes on, each at its input's location; a binding's buffer is its
        // last attribute's, as glVertexArrayVertexBuffer leaves it)
        VkDraw::VertexInput in;
        for (size_t i = 0; i < layout.attribs.size(); ++i) {
            if (!(on & (1u << i))) {
                continue;
            }
            const VertexLayout::Attrib& a = layout.attribs[i];
            VkDraw::VertexAttrib va;
            va.location = (uint32_t)v.locations[i];
            va.binding = a.binding;
            va.size = a.size;
            va.type = a.type;
            va.normalized = a.normalized != GL_FALSE;
            va.offset = a.offset;
            in.attribs.push_back(va);
            if (a.binding < (GLuint)VkDraw::VertexInput::kMaxBindings) {
                in.buffers[a.binding] = buffers[i];
                in.offsets[a.binding] = (uint64_t)offsets[i];
                in.strides[a.binding] = (uint64_t)strides[i];
            }
        }
        for (size_t b = 0; b < layout.divisors.size() && b < (size_t)VkDraw::VertexInput::kMaxBindings; ++b) {
            in.divisors[b] = layout.divisors[b];
        }
        in.element_buffer = element_buffer;
        VkDraw::SetVertexInput(in);
        v.enabled = on;
        return;
    }
    uint32_t changed = on ^ v.enabled;
    for (size_t i = 0; changed != 0; ++i, changed >>= 1) {
        if (changed & 1u) {
            if (on & (1u << i)) {
                glEnableVertexArrayAttrib(v.vao, v.locations[i]);
            } else {
                glDisableVertexArrayAttrib(v.vao, v.locations[i]);
            }
        }
    }
    v.enabled = on;
    glVertexArrayElementBuffer(v.vao, element_buffer);
}

void Graphics::BeginVertexLayout(VertexLayout& layout, int program, VBOContainer* const* vbos, GLuint element_buffer) {
    GLuint buffers[32] = {0};
    GLintptr offsets[32] = {0};
    GLsizei strides[32] = {0};
    uint32_t enabled = 0;
    LOG_ASSERT(layout.Count() <= 32);
    for (int i = 0; i < layout.Count(); ++i) {
        strides[i] = layout.attribs[i].size * sizeof(GLfloat);
        if (vbos[i] && vbos[i]->valid()) {
            buffers[i] = vbos[i]->id();
            enabled |= 1u << i;
        }
    }
    BeginVertexLayout(layout, program, enabled, buffers, offsets, strides, element_buffer);
}

void Graphics::EndVertexLayout() {
    if (vertex_layout_bound_) {
        if (UseVulkan()) {
            VkDraw::ClearVertexInput();
        } else {
            glBindVertexArray(vao_);
        }
        vertex_layout_bound_ = false;
        layout_vao_bound_ = 0;
    }
}

void Graphics::EnableVertexAttribArray(unsigned int index) {
    if (vertex_layout_bound_) {
        LayoutLeftBound(this, "EnableVertexAttribArray");
    }
    assert(index < sizeof(wanted_vertex_attrib_arrays) * 8);
    wanted_vertex_attrib_arrays |= (1 << index);
}

void Graphics::DisableVertexAttribArray(unsigned int index) {
    assert(index < sizeof(wanted_vertex_attrib_arrays) * 8);
    wanted_vertex_attrib_arrays &= ~(1 << index);
}

void Graphics::ResetVertexAttribArrays() {
    wanted_vertex_attrib_arrays = 0;
}

#ifdef __GNUC__

unsigned char BitScanForward(unsigned long* index, unsigned long mask) {
    int temp = __builtin_ffs(mask);

    if (temp == 0) {
        *index = 0;
        return 0;
    } else {
        *index = temp - 1;
        return 1;
    }
}

#else  // __GNUC__

#ifndef _MSC_VER
#error Unknown compiler
#endif  // _MSC_VER

#define BitScanForward _BitScanForward

#endif  // __GNUC__

void Graphics::SetVertexAttribArrays() {
    if (vertex_layout_bound_) {
        return;  // (a layout's VAO has its own)
    }
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        active_vertex_attrib_arrays = wanted_vertex_attrib_arrays;
        return;
    }
    uint32_t changed_vertex_attrib_arrays = active_vertex_attrib_arrays ^ wanted_vertex_attrib_arrays;

    while (changed_vertex_attrib_arrays != 0) {
        unsigned long bit = 0;
        unsigned char nonZero = BitScanForward(&bit, changed_vertex_attrib_arrays);
        // can't be zero
        LOG_ASSERT(nonZero != 0);
        unsigned int mask = 1u << bit;
        LOG_ASSERT(mask != 0);

        if ((wanted_vertex_attrib_arrays & mask) != 0) {
            glEnableVertexAttribArray(bit);
        } else {
            glDisableVertexAttribArray(bit);
        }

        changed_vertex_attrib_arrays ^= mask;

#ifdef DEBUG
        active_vertex_attrib_arrays ^= mask;
#endif  // DEBUG
    }

#ifdef DEBUG
    LOG_ASSERT_EQ(active_vertex_attrib_arrays, wanted_vertex_attrib_arrays);
#endif  // DEBUG

    active_vertex_attrib_arrays = wanted_vertex_attrib_arrays;
}

void Graphics::ClearGLState() {
    // reset everything in case someone else has changed these
    active_vertex_attrib_arrays = 0;
    wanted_vertex_attrib_arrays = 0;
    if (UseVulkan()) {  // (Vulkan: stage 5.5)
        return;
    }
    for (int i = 0; i < max_vertex_attrib_arrays; i++) {
        glDisableVertexAttribArray(i);
    }
}

void Graphics::SetUpShadowTextures() {
    PROFILER_GPU_ZONE(g_profiler_ctx, "Set up shadow textures");
    Textures* textures = Textures::Instance();

    PushRenderTarget();

    if (first_load) {
        static_shadow_depth_ref = textures->makeTexture(cascade_shadow_res, cascade_shadow_res, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT);
        textures->SetTextureName(static_shadow_depth_ref, "Static Shadow Depth");

        textures->setFilters(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        static_shadow_target = RenderTarget().Depth(static_shadow_depth_ref);
        BindRenderTarget(static_shadow_target);

        textures->SetSamplingParameter(static_shadow_depth_ref, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        textures->SetSamplingParameter(static_shadow_depth_ref, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);  // (reversed-Z: lit when nearer or level)
        CHECK_FBO_ERROR();
    }

    cascade_shadow_target = RenderTarget();
    cascade_shadow_color_target = RenderTarget();

    cascade_shadow_depth_ref = TextureRef();
    cascade_shadow_color_ref = TextureRef();

    if (!g_simple_shadows && g_level_shadows) {
        cascade_shadow_depth_ref = textures->makeTexture(cascade_shadow_res, cascade_shadow_res, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT);
        textures->SetTextureName(cascade_shadow_depth_ref, "Cascade Shadow Depth");

        textures->setFilters(GL_NEAREST, GL_NEAREST);
        cascade_shadow_color_ref = textures->makeTexture(2048, 2048, GL_RGBA, GL_RGBA);
        textures->SetTextureName(cascade_shadow_color_ref, "Cascade Shadow Color");

        textures->setFilters(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        cascade_shadow_target = RenderTarget().Depth(cascade_shadow_depth_ref);
        cascade_shadow_color_target = RenderTarget().Color(cascade_shadow_color_ref).Depth(cascade_shadow_depth_ref);
        BindRenderTarget(cascade_shadow_target);

        textures->SetSamplingParameter(cascade_shadow_depth_ref, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        textures->SetSamplingParameter(cascade_shadow_depth_ref, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);  // (reversed-Z: lit when nearer or level)
    }

    PopRenderTarget();

    Textures::Instance()->InvalidateBindCache();
}

void Graphics::DebugTracePrint(const char* message) {
#if GPU_MARKERS
    if (UseVulkan()) {  // (Vulkan: stage 5.6)
        VkBackend::InsertLabel(message);
        return;
    }
    if (GLAD_GL_KHR_debug) {
        glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, message);
    }
#endif  // GPU_MARKERS
}

vec3 GraphicsConfig::BloodColorFromString(const std::string& blood_color_str) {
    vec3 blood_color(0.5f, 0.0f, 0.0f);
    {
        int num_chars = blood_color_str.length();
        blood_color[0] = (float)atof(&blood_color_str[0]);
        int which_num = 1;
        for (int i = 0; i < num_chars - 1; ++i) {
            if (blood_color_str[i] == ' ') {
                blood_color[which_num] = (float)atof(&blood_color_str[i + 1]);
                ++which_num;
                if (which_num > 2) {
                    break;
                }
            }
        }
    }
    return blood_color;
}
