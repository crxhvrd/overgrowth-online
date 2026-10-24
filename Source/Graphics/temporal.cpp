//-----------------------------------------------------------------------------
//           Name: temporal.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See temporal.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "temporal.h"

#include <Graphics/camera.h>
#include <Graphics/glstate.h>
#include <Graphics/graphics.h>
#include <Graphics/shaders.h>
#include <Graphics/textures.h>
#include <Graphics/vbocontainer.h>
#include <Graphics/render_backend.h>
#include <Graphics/Vulkan/vk_ffx.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Graphics/Vulkan/vk_ngx.h>
#include <Internal/profiler.h>
#include <Internal/config.h>
#include <Logging/logdata.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Temporal {
namespace {

// Halton (2, 3): 8 phases at the render size, as many as converge between two fast
// motions; DLSS asks for 8 times the scale's area ratio (more samples per output pixel)
const int kPhases = 8;
// A jump of the camera this far (metres), or a turn this sharp in one frame, is a
// cut: the history is of another view
const float kCutDistance = 2.5f;
const float kCutCosine = 0.5f;

enum Resolver { kResolverNone, kResolverTAA, kResolverDLSS, kResolverFSR };

struct State {
    bool in_scene = false;
    bool pending = false;  // a scene began: Resolve to come
    uint64_t frame = 0;
    int width = 0, height = 0;          // the scene's (render) size
    int out_width = 0, out_height = 0;  // the post effects'
    int phases = kPhases;
    vec2 jitter;
    mat4 view_proj, prev_view_proj;
    bool have_prev = false;
    vec3 prev_cam_pos, prev_cam_dir;
    bool history_valid = false;
    // What resolved the last frame (its history is the one to go on from: TAA's after a
    // DLSS frame is stale, DLSS's own starts over after a TAA one)
    int resolver = kResolverNone;
    TextureRef history[2];  // TAA's, at the scene's size
    TextureRef motion;      // at the scene's size
    TextureRef output;      // DLSS's and FSR's, at the post effects' size
    int current = 0;        // history[current] is written this frame, the other read
    // FSR: the frame as it was before the transparent things and particles (OpaqueDrawn),
    // and the reactive mask FSR makes from it; both at the scene's size
    TextureRef opaque, reactive;
    bool opaque_drawn = false;  // (this frame)
    float camera_near = 0.1f, fov_vertical = 1.0f;
    std::chrono::steady_clock::time_point last_scene;
    float frame_ms = 16.7f;
};
State s;

float Halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

glm::dmat4 ToDouble(const mat4& m) {
    double d[16];
    for (int i = 0; i < 16; ++i) {
        d[i] = (double)m.entries[i];
    }
    return glm::make_mat4(d);
}

// From this frame's clip space (unjittered) to last frame's: the motion of what
// stands still (double precision: a camera far from the origin)
mat4 Reprojection() {
    const glm::dmat4 r = ToDouble(s.prev_view_proj) * glm::inverse(ToDouble(s.view_proj));
    mat4 out;
    const double* p = glm::value_ptr(r);
    for (int i = 0; i < 16; ++i) {
        out.entries[i] = (float)p[i];
    }
    return out;
}

void EnsureTargets(int width, int height, int out_width, int out_height) {
    Textures* textures = Textures::Instance();
    if (!s.motion.valid() || s.width != width || s.height != height) {
        s.history[0] = textures->makeRectangularTexture(width, height, GL_RGBA16F, GL_RGBA);
        textures->SetTextureName(s.history[0], "TAA::History 0");
        s.history[1] = textures->makeRectangularTexture(width, height, GL_RGBA16F, GL_RGBA);
        textures->SetTextureName(s.history[1], "TAA::History 1");
        s.motion = textures->makeRectangularTexture(width, height, GL_RG16F, GL_RG);
        textures->SetTextureName(s.motion, "TAA::Motion");
        s.opaque.clear();  // (made again at their next use)
        s.reactive.clear();
        s.width = width;
        s.height = height;
        s.history_valid = false;
        s.have_prev = false;
    }
    if (s.output.valid() && (s.out_width != out_width || s.out_height != out_height)) {
        s.output.clear();  // (made again at its next use)
    }
    s.out_width = out_width;
    s.out_height = out_height;
}

void DrawQuad(int shader_id, const VBOContainer& quad_vert_vbo, const VBOContainer& quad_index_vbo) {
    static VertexLayout layout;
    if (layout.Count() == 0) {
        layout.Add("vert_attrib", 0, 2);
    }
    GLuint buffers[1] = {quad_vert_vbo.id()};
    GLintptr offsets[1] = {0};
    GLsizei strides[1] = {2 * sizeof(GLfloat)};
    Graphics* graphics = Graphics::Instance();
    graphics->BeginVertexLayout(layout, shader_id, quad_vert_vbo.valid() ? 1u : 0u, buffers, offsets, strides, quad_index_vbo.id());
    graphics->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    graphics->EndVertexLayout();
}

bool IsDlss(int mode) {
    return mode >= kDLAA && mode <= kDLSSUltraPerformance;
}

bool IsFsr(int mode) {
    return mode >= kFSRNative && mode <= kFSRUltraPerformance;
}

// The scene's size over the render size for an FSR mode (FSR's own ratios)
float FsrScale(int mode) {
    switch (mode) {
        case kFSRQuality: return 1.0f / 1.5f;
        case kFSRBalanced: return 1.0f / 1.7f;
        case kFSRPerformance: return 0.5f;
        case kFSRUltraPerformance: return 1.0f / 3.0f;
        default: return 1.0f;
    }
}

// The scene's size over the render size for a DLSS mode (DLSS's own ratios)
float DlssScale(int mode) {
    switch (mode) {
        case kDLSSQuality: return 1.0f / 1.5f;
        case kDLSSBalanced: return 1.0f / 1.7241f;
        case kDLSSPerformance: return 0.5f;
        case kDLSSUltraPerformance: return 1.0f / 3.0f;
        default: return 1.0f;
    }
}

// NVSDK_NGX_PerfQuality_Value for a DLSS mode
int DlssQuality(int mode) {
    switch (mode) {
        case kDLSSQuality: return 2;           // MaxQuality
        case kDLSSBalanced: return 1;          // Balanced
        case kDLSSPerformance: return 0;       // MaxPerf
        case kDLSSUltraPerformance: return 3;  // UltraPerformance
        default: return 5;                     // DLAA
    }
}

}  // namespace

int Mode() {
    return Graphics::Instance()->config_.temporal_aa();
}

bool Enabled() {
    return Mode() != kOff && Graphics::Instance()->features().HDR_enable();
}

float RenderScale() {
    const int mode = Mode();
    if (!Enabled() || !UseVulkan()) {
        return 1.0f;
    }
    float scale = 1.0f;
    if (IsDlss(mode) && VkNgx::DlssAvailable()) {
        scale = DlssScale(mode);
    } else if (IsFsr(mode) && VkFfx::Available()) {
        scale = FsrScale(mode);
    }
    if (scale >= 1.0f) {
        return 1.0f;
    }
    // (diagnostics: another scale, 0.33 to 1, the mode's quality setting kept)
    const float custom = config["dlss_render_scale"].toNumber<float>();
    if (custom > 0.0f) {
        return std::max(0.33f, std::min(1.0f, custom));
    }
    return scale;
}

bool BeginScene(Camera* camera, int width, int height, int out_width, int out_height) {
    s.in_scene = false;
    s.pending = false;
    Graphics* graphics = Graphics::Instance();
    if (!Enabled() || !graphics->screen_mv_tex.valid() || width <= 0 || height <= 0) {
        s.history_valid = false;
        s.have_prev = false;
        return false;
    }
    EnsureTargets(width, height, out_width, out_height);

    const float ratio = (float)out_width / (float)width;
    s.phases = std::max(kPhases, (int)std::ceil((float)kPhases * ratio * ratio - 0.01f));
    const int phase = (int)(s.frame % (uint64_t)s.phases) + 1;
    s.jitter = vec2(Halton(phase, 2) - 0.5f, Halton(phase, 3) - 0.5f);
    camera->SetJitter(s.jitter[0] * 2.0f / (float)width, s.jitter[1] * 2.0f / (float)height);
    // Textures in the scene as sharp as at the render size (the upscaler's due: their
    // mips chosen for the output's pixels, not the scene's)
    const float bias_mult = config["upscaler_lod_bias"].toNumber<float>();  // (1: the scale's; 0: none)
    Textures::Instance()->SetSceneLodBias(width < out_width ? bias_mult * std::log2((float)width / (float)out_width) : 0.0f);

    const mat4 view = camera->GetViewMatrix();
    const mat4 proj = camera->GetUnjitteredProjMatrix();
    s.view_proj = proj * view;
    // (FSR's camera: the near plane, the vertical field of view; the time since the last)
    s.camera_near = camera->GetNearPlane();
    s.fov_vertical = 2.0f * std::atan(1.0f / std::max(1e-6f, proj.entries[5]));
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    s.frame_ms = std::max(0.1f, std::min(200.0f, std::chrono::duration<float, std::milli>(now - s.last_scene).count()));
    s.last_scene = now;
    s.opaque_drawn = false;
    const mat4 inv_view = invert(view);
    const vec3 cam_pos = inv_view.GetTranslationPart();
    const vec3 cam_dir = normalize(inv_view.GetRotatedvec3(vec3(0.0f, 0.0f, -1.0f)));
    if (s.have_prev && (distance(cam_pos, s.prev_cam_pos) > kCutDistance || dot(cam_dir, s.prev_cam_dir) < kCutCosine)) {
        s.have_prev = false;
    }
    if (!s.have_prev) {
        s.prev_view_proj = s.view_proj;
        s.history_valid = false;
    }
    s.prev_cam_pos = cam_pos;
    s.prev_cam_dir = cam_dir;
    s.in_scene = true;
    s.pending = true;
    return true;
}

void EndScene(Camera* camera) {
    camera->SetJitter(0.0f, 0.0f);
    if (s.in_scene) {
        Textures::Instance()->SetSceneLodBias(0.0f);
    }
    s.in_scene = false;
}

bool InScene() {
    return s.in_scene;
}

void OpaqueDrawn() {
    if (!s.in_scene || !IsFsr(Mode()) || !UseVulkan() || !VkFfx::Available()) {
        return;
    }
    const bool reactive_off = config["fsr_reactive"].toNumber<int>() < 0;  // (diagnostics: -1 none)
    if (reactive_off) {
        return;
    }
    Graphics* graphics = Graphics::Instance();
    Textures* textures = Textures::Instance();
    if (!s.opaque.valid()) {
        s.opaque = textures->makeRectangularTexture(s.width, s.height, GL_RGBA16F, GL_RGBA);
        textures->SetTextureName(s.opaque, "FSR::Opaque Color");
        s.reactive = textures->makeRectangularTexture(s.width, s.height, GL_R16F, GL_RED);  // (taa #REACTIVE draws it)
        textures->SetTextureName(s.reactive, "FSR::Reactive");
    }
    PROFILER_GPU_ZONE(g_profiler_ctx, "FSR opaque copy");
    const PixelRect all(0, 0, s.width, s.height);
    graphics->PushRenderTarget();
    graphics->BlitRenderTarget(RenderTarget().Color(graphics->screen_color_tex), 0, all, RenderTarget().Color(s.opaque), 0, all);
    graphics->PopRenderTarget();
    s.opaque_drawn = true;
}

std::string MotionSuffix(const char* global_suffix) {
    std::string suffix = global_suffix;
    const std::string no_velocity = "#NO_VELOCITY_BUF";
    for (size_t at = suffix.find(no_velocity); at != std::string::npos; at = suffix.find(no_velocity)) {
        suffix.erase(at, no_velocity.size());
    }
    return "#TEMPORAL_MV " + suffix;
}

bool Resolve(const VBOContainer& quad_vert_vbo, const VBOContainer& quad_index_vbo) {
    if (!s.pending) {
        return false;
    }
    s.pending = false;
    PROFILER_GPU_ZONE(g_profiler_ctx, "Temporal AA");
    Graphics* graphics = Graphics::Instance();
    Shaders* shaders = Shaders::Instance();
    Textures* textures = Textures::Instance();

    GLState gl_state;
    gl_state.blend = false;
    gl_state.cull_face = false;
    gl_state.depth_test = false;
    gl_state.depth_write = false;
    graphics->setGLState(gl_state);
    graphics->PushViewport();
    graphics->PushRenderTarget();
    graphics->setViewport(0, 0, s.width, s.height);

    {  // The motion of every pixel: what moves wrote its own, the rest follows the camera
        PROFILER_GPU_ZONE(g_profiler_ctx, "Motion");
        int shader_id = shaders->returnProgram("taa #MOTION");
        shaders->setProgram(shader_id);
        shaders->SetUniformInt("screen_width", s.width);
        shaders->SetUniformInt("screen_height", s.height);
        shaders->SetUniformMat4("reprojection", Reprojection());
        textures->bindTexture(graphics->screen_depth_tex, 1);
        textures->bindTexture(graphics->screen_mv_tex, 2);
        graphics->BindRenderTarget(s.motion);
        DrawQuad(shader_id, quad_vert_vbo, quad_index_vbo);
    }

    const int write = s.current, read = s.current ^ 1;
    // DLAA and DLSS: DLSS resolves the frame (its own history) up to the post effects'
    // size where it can run; TAA at the scene's size otherwise
    TextureRef result;
    int result_width = s.width, result_height = s.height;
    bool dlss = false;
    const int mode = Mode();
    if (IsDlss(mode) && UseVulkan()) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "DLSS");
        if (!s.output.valid()) {
            vkgl::NextTextureWritable();  // (DLSS writes it)
            s.output = textures->makeRectangularTexture(s.out_width, s.out_height, GL_RGBA16F, GL_RGBA);
            textures->SetTextureName(s.output, "DLSS::Output");
        }
        const float jitter_sign = config["dlss_jitter_sign"].toNumber<float>() < 0.0f ? -1.0f : 1.0f;  // (diagnostics)
        const float motion_sign = config["dlss_motion_sign"].toNumber<float>() > 0.0f ? 1.0f : -1.0f;  // (diagnostics)
        VkNgx::DlssFrame f;
        f.color = textures->returnTexture(graphics->screen_color_tex);
        f.depth = textures->returnTexture(graphics->screen_depth_tex);
        f.motion = textures->returnTexture(s.motion);
        f.output = textures->returnTexture(s.output);
        f.render_width = (uint32_t)s.width;
        f.render_height = (uint32_t)s.height;
        f.output_width = (uint32_t)s.out_width;
        f.output_height = (uint32_t)s.out_height;
        // (both in the images' own rows: GL's, bottom-up, the same for every input; the
        // jitter in the scene's pixels)
        f.jitter_x = jitter_sign * s.jitter[0];
        f.jitter_y = jitter_sign * s.jitter[1];
        // Our motion is this frame's uv minus last frame's; DLSS's, the scene's pixels to
        // last frame
        f.motion_scale_x = motion_sign * (float)s.width;
        f.motion_scale_y = motion_sign * (float)s.height;
        f.reset = !s.history_valid || s.resolver != kResolverDLSS;
        f.quality = DlssQuality(mode);
        f.preset = config["dlss_preset"].toNumber<int>();
        dlss = VkNgx::EvaluateDlss(f);
        if (dlss) {
            result = s.output;
            result_width = s.out_width;
            result_height = s.out_height;
        }
    }
    bool fsr = false;
    if (IsFsr(mode) && UseVulkan()) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "FSR");
        if (!s.output.valid()) {
            vkgl::NextTextureWritable();  // (FSR writes it)
            s.output = textures->makeRectangularTexture(s.out_width, s.out_height, GL_RGBA16F, GL_RGBA);
            textures->SetTextureName(s.output, "FSR::Output");
        }
        VkFfx::FsrFrame f;
        f.color = textures->returnTexture(graphics->screen_color_tex);
        f.depth = textures->returnTexture(graphics->screen_depth_tex);
        f.motion = textures->returnTexture(s.motion);
        f.output = textures->returnTexture(s.output);
        if (s.opaque_drawn) {
            // The reactive mask: what the transparent things and particles drew, the frame
            // against its opaque part (our pass: FSR's own would be a second dispatch)
            PROFILER_GPU_ZONE(g_profiler_ctx, "FSR reactive mask");
            int shader_id = shaders->returnProgram("taa #REACTIVE");
            shaders->setProgram(shader_id);
            textures->bindTexture(s.opaque, 0);
            textures->bindTexture(graphics->screen_color_tex, 1);
            graphics->BindRenderTarget(s.reactive);
            DrawQuad(shader_id, quad_vert_vbo, quad_index_vbo);
            f.reactive = textures->returnTexture(s.reactive);
        }
        f.render_width = (uint32_t)s.width;
        f.render_height = (uint32_t)s.height;
        f.output_width = (uint32_t)s.out_width;
        f.output_height = (uint32_t)s.out_height;
        // (as DLSS's: the images' own rows, the jitter in the scene's pixels; the motion
        // to pixels toward last frame)
        f.jitter_x = s.jitter[0];
        f.jitter_y = s.jitter[1];
        f.motion_scale_x = -(float)s.width;
        f.motion_scale_y = -(float)s.height;
        f.reset = !s.history_valid || s.resolver != kResolverFSR;
        f.sharpness = std::max(0.0f, std::min(1.0f, config["fsr_sharpness"].toNumber<float>()));
        f.frame_ms = s.frame_ms;
        f.camera_near = s.camera_near;
        f.fov_vertical = s.fov_vertical;
        fsr = VkFfx::EvaluateFsr(f);
        if (fsr) {
            result = s.output;
            result_width = s.out_width;
            result_height = s.out_height;
        }
    }
    const bool dlss_ran = dlss;
    dlss = dlss || fsr;  // (the TAA below for neither)
    if (!dlss) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "Resolve");
        int shader_id = shaders->returnProgram("taa #RESOLVE");
        shaders->setProgram(shader_id);
        shaders->SetUniformInt("screen_width", s.width);
        shaders->SetUniformInt("screen_height", s.height);
        shaders->SetUniformVec2("jitter", s.jitter);
        shaders->SetUniformFloat("history_valid", s.history_valid && s.resolver == kResolverTAA ? 1.0f : 0.0f);
        const int debug_view = config["temporal_aa_debug"].toNumber<int>();  // (diagnostics)
        shaders->SetUniformInt("debug_view", debug_view);
        textures->bindTexture(graphics->screen_color_tex, 0);
        textures->bindTexture(graphics->screen_depth_tex, 1);
        textures->bindTexture(s.motion, 2);
        textures->bindTexture(s.history[read], 3);
        graphics->BindRenderTarget(s.history[write]);
        DrawQuad(shader_id, quad_vert_vbo, quad_index_vbo);
        result = s.history[write];
    }

    // On to where the post effects start (PostColor: they draw into its levels; the
    // history stays as it was resolved), sharpened on the way; brought up to that size
    // if it's still the scene's (DLSS didn't run)
    const TextureRef& dest = graphics->PostColor();
    const int dest_width = graphics->Upscaling() ? graphics->render_dims[0] : s.width;
    const int dest_height = graphics->Upscaling() ? graphics->render_dims[1] : s.height;
    // (FSR's sharpened in its own pass already)
    const float sharpness = fsr ? 0.0f : std::max(0.0f, std::min(1.0f, config[dlss ? "dlss_sharpness" : "temporal_aa_sharpness"].toNumber<float>()));
    if (result_width != dest_width || result_height != dest_height) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "Upscale (plain)");
        graphics->BlitRenderTarget(RenderTarget().Color(result), 0, PixelRect(0, 0, result_width, result_height), RenderTarget().Color(dest), 0,
                                   PixelRect(0, 0, dest_width, dest_height), false, true);
    } else if (sharpness > 0.0f) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "Sharpen");
        graphics->setViewport(0, 0, dest_width, dest_height);
        int shader_id = shaders->returnProgram("taa #SHARPEN");
        shaders->setProgram(shader_id);
        shaders->SetUniformInt("screen_width", dest_width);
        shaders->SetUniformInt("screen_height", dest_height);
        shaders->SetUniformFloat("sharpness", sharpness);
        textures->bindTexture(result, 0);
        graphics->BindRenderTarget(dest);
        DrawQuad(shader_id, quad_vert_vbo, quad_index_vbo);
    } else {
        PROFILER_GPU_ZONE(g_profiler_ctx, "Copy");
        const PixelRect all(0, 0, dest_width, dest_height);
        graphics->BlitRenderTarget(RenderTarget().Color(result), 0, all, RenderTarget().Color(dest), 0, all);
    }

    graphics->PopRenderTarget();
    graphics->PopViewport();
    graphics->ResetVertexAttribArrays();

    s.current = read;
    s.resolver = fsr ? kResolverFSR : dlss_ran ? kResolverDLSS : kResolverTAA;
    s.history_valid = true;
    s.prev_view_proj = s.view_proj;
    s.have_prev = true;
    ++s.frame;
    return true;
}

const mat4& ViewProj() {
    return s.view_proj;
}

const mat4& PrevViewProj() {
    return s.prev_view_proj;
}

vec2 JitterPixels() {
    return s.jitter;
}

uint64_t Frame() {
    return s.frame;
}

void Reset() {
    s.history_valid = false;
    s.have_prev = false;
}

}  // namespace Temporal
