//-----------------------------------------------------------------------------
//           Name: graphics.h
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
#pragma once

#include <map>
#include <string>

#include <Math/vec2.h>
#include <Math/mat4.h>
#include <Math/mat3.h>

#include <Internal/error.h>
#include <Internal/hardware_specs.h>

#include <Graphics/glstate.h>
#include <Asset/Asset/texture.h>

#include <opengl.h>

#include <queue>
#include <vector>
#include <stack>
#include <climits>
#include <stdint.h>  // cstdint would be more c++ but os x doesn't understand it

#define UBO_INSTANCE_INFO 0
#define UBO_CLUSTER_DATA 1
#define UBO_SHADOW_CASCADES 2
// (3: the default uniform block, Shaders::kDefaultBlockBinding)
#define UBO_LIGHT_PROBE_INFO 4  // Overgrowth Online (stage 5.0): the uniform blocks get these at link

class UniformRingBuffer;

const GLuint INVALID_FRAMEBUFFER = UINT_MAX;

enum ShadowedClientStateFlags {
    F_VERTEX_ARRAY = (1 << 0),
    F_NORMAL_ARRAY = (1 << 1),
    F_COLOR_ARRAY = (1 << 2),
    F_TEXTURE_COORD_ARRAY0 = (1 << 3),
    F_TEXTURE_COORD_ARRAY1 = (1 << 4),
    F_TEXTURE_COORD_ARRAY2 = (1 << 5),
    F_TEXTURE_COORD_ARRAY3 = (1 << 6),
    F_TEXTURE_COORD_ARRAY4 = (1 << 7),
    F_TEXTURE_COORD_ARRAY5 = (1 << 8),
    F_TEXTURE_COORD_ARRAY6 = (1 << 9),
    F_TEXTURE_COORD_ARRAY7 = (1 << 10)
};

enum ShadowedClientStateID {
    CS_VERTEX_ARRAY,
    CS_NORMAL_ARRAY,
    CS_COLOR_ARRAY,
    CS_TEXTURE_COORD_ARRAY0,
    CS_TEXTURE_COORD_ARRAY1,
    CS_TEXTURE_COORD_ARRAY2,
    CS_TEXTURE_COORD_ARRAY3,
    CS_TEXTURE_COORD_ARRAY4,
    CS_TEXTURE_COORD_ARRAY5,
    CS_TEXTURE_COORD_ARRAY6,
    CS_TEXTURE_COORD_ARRAY7,

    CS_MAX_SHADOWED_CLIENT_STATES
};

class Config;

namespace BloodLevel {
enum Type {
    kNone = 0,
    kSimple = 1,
    kFull = 2
};
}

namespace FullscreenMode {
enum Mode {
    kWindowed = 0  // Resizable floating window
    ,
    kFullscreen = 1  // Exclusive fullscreen
    ,
    kWindowed_borderless = 2,
    kFullscreen_borderless = 3
};
}

class GraphicsConfig {
   public:
    bool dynamic_character_cubemap_;
    float anisotropy() const { return anisotropy_; }
    int FSAA_samples() const { return FSAA_samples_; }
    FullscreenMode::Mode full_screen() const { return full_screen_; }
    int target_monitor() const { return target_monitor_; }
    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }
    bool split_screen() const { return split_screen_; }
    int texture_reduce() const { return texture_reduce_; }
    int texture_reduction_factor() const { return texture_reduction_factor_; }
    bool vSync() const { return vSync_; }
    bool limit_fps_in_game() const { return limit_fps_in_game_; }
    int max_frame_rate() const { return max_frame_rate_; }
    int blood() const { return blood_; }
    bool simple_fog() const { return simple_fog_; }
    bool depth_of_field() const { return depth_of_field_; }
    bool depth_of_field_reduced() const { return depth_of_field_reduced_; }
    bool detail_objects() const { return detail_objects_; }
    bool detail_object_decals() const { return detail_object_decals_; }
    bool detail_object_lowres() const { return detail_object_lowres_; }
    bool detail_object_shadows() const { return detail_object_shadows_; }
    const vec3& blood_color() const { return blood_color_; }
    static vec3 BloodColorFromString(const std::string& str);

    void SetAnisotropy(float val) { anisotropy_ = val; }
    void SetFSAASamples(int val) { FSAA_samples_ = val; }
    void SetFullscreen(FullscreenMode::Mode val) { full_screen_ = val; }
    void SetTargetMonitor(int val) { target_monitor_ = val; }
    void SetScreenWidth(int val) { screen_width_ = val; }
    void SetScreenHeight(int val) { screen_height_ = val; }
    void SetSplitScreen(bool val) { split_screen_ = val; }
    void SetBlood(int val) { blood_ = val; }
    void SetBloodColor(const vec3& val) { blood_color_ = val; }
    void SetTextureReduce(int val) {
        texture_reduce_ = val;
        texture_reduction_factor_ = 1 << texture_reduce_;
    }
    void SetVSync(bool val) { vSync_ = val; }
    void SetLimitFpsInGame(bool val) { limit_fps_in_game_ = val; }
    void SetMaxFrameRate(int val) { max_frame_rate_ = val; }
    void SetSimpleFog(bool val) { simple_fog_ = val; }
    void SetDepthOfField(bool val) { depth_of_field_ = val; }
    void SetDepthOfFieldReduced(bool val) { depth_of_field_reduced_ = val; }
    void SetDetailObjects(bool val) { detail_objects_ = val; }
    void SetDetailObjectDecals(bool val) { detail_object_decals_ = val; }
    void SetDetailObjectLowres(bool val) { detail_object_lowres_ = val; }
    void SetDetailObjectShadows(bool val) { detail_object_shadows_ = val; }

    bool seamless_cubemaps_;
    float motion_blur_amount_;

   private:
    bool simple_fog_;
    bool depth_of_field_;
    bool depth_of_field_reduced_;
    bool detail_objects_;
    bool detail_object_decals_;
    bool detail_object_lowres_;
    bool detail_object_shadows_;
    float anisotropy_;
    int FSAA_samples_;
    FullscreenMode::Mode full_screen_;
    int target_monitor_;  // Used when retrieving monitor data from SDL, though not 100% supported. Currently hard-coded to 0
    int screen_width_;
    int screen_height_;
    bool split_screen_;
    int texture_reduce_;
    int texture_reduction_factor_;
    bool vSync_;
    bool limit_fps_in_game_;
    int max_frame_rate_;
    int blood_;
    vec3 blood_color_;
};

class GraphicsFeatures {
   public:
    bool HDR_enable() const { return HDR_enable_; }
    bool frame_buffer_fsaa_enabled() const { return frame_buffer_fsaa_enabled_; }

    void SetHDREnable(bool val) { HDR_enable_ = val; }
    void SetFrameBufferFSAAEnabled(bool val) { frame_buffer_fsaa_enabled_ = val; }

   private:
    bool HDR_enable_;
    bool frame_buffer_fsaa_enabled_;
};

struct GraphicsShadowState {
    int depth_func_type_;  // Shadow the glDepthFunc values
    bool depth_func_unset_;

    GLState gl_state_;

    bool client_states_[CS_MAX_SHADOWED_CLIENT_STATES];
    int client_active_textures_;
    bool polygon_offset_enable_;
    // Overgrowth Online (GL 4.6 stage 2.6): the rest of the fixed-function state,
    // set only through Graphics (GL's defaults)
    bool alpha_to_coverage_ = false;
    bool scissor_test_ = false;
    unsigned char color_mask_ = 0xF;  // red, green, blue, alpha: bits 0-3
};

struct SDL_Window;

struct ViewportDims {
    int entries[4];
};

// Overgrowth Online (GL 4.6 stage 5.0): what a pass draws into, the backend-neutral
// way (Vulkan: dynamic rendering). Up to four colour attachments and a depth one,
// each a texture at a mip level and, for cube maps and texture arrays, a layer (a
// cube face is layer 0-5); a multisample texture for MSAA. No attachments: the window
struct RenderTarget {
    static const int kMaxColor = 4;
    struct Attachment {
        TextureRef tex;
        int level;
        int layer;  // -1: the 2D texture itself
        Attachment() : level(0), layer(-1) {}
    };
    Attachment color[kMaxColor];
    int num_color;
    Attachment depth;

    RenderTarget() : num_color(0) {}
    RenderTarget& Color(const TextureRef& t, int level = 0, int layer = -1) {
        color[num_color].tex = t;
        color[num_color].level = level;
        color[num_color].layer = layer;
        ++num_color;
        return *this;
    }
    RenderTarget& Depth(const TextureRef& t, int level = 0, int layer = -1) {
        depth.tex = t;
        depth.level = level;
        depth.layer = layer;
        return *this;
    }
    bool IsWindow() const { return num_color == 0 && !depth.tex.valid(); }
};

struct PixelRect {
    int x, y, w, h;
    PixelRect() : x(0), y(0), w(0), h(0) {}
    PixelRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
};

struct PostEffects {
    TextureRef temp_screen_tex;
    TextureRef tone_mapped_tex;
};

// Overgrowth Online (GL 4.6 stage 2.4): a vertex format. Each attribute names the
// shader input it feeds, its format and the buffer binding it reads. A VAO is made
// per set of input locations (programs that place the inputs alike share one), so
// the format is set once, not per draw. Graphics::BeginVertexLayout binds it with
// this draw's buffers; EndVertexLayout goes back to the engine's VAO
class VBOContainer;

class VertexLayout {
   public:
    struct Attrib {
        std::string name;
        GLuint binding;
        GLint size;
        GLenum type;
        GLboolean normalized;
        GLuint offset;  // within the binding's element
    };
    void Add(const char* name, GLuint binding, GLint size, GLenum type = GL_FLOAT, GLboolean normalized = GL_FALSE, GLuint offset = 0);
    void SetDivisor(GLuint binding, GLuint divisor);
    int Count() const { return (int)attribs.size(); }

   private:
    friend class Graphics;
    struct Vao {
        GLuint vao;
        std::vector<GLint> locations;  // each attribute's input location (-1: not in the program)
        uint32_t enabled;              // attributes on
    };
    std::vector<Attrib> attribs;
    std::vector<GLuint> divisors;                 // per binding
    std::vector<Vao> vaos;
    std::vector<std::pair<int, int> > program_vao;  // program -> index in vaos
};

class Graphics {
   public:
    void Dispose();

   private:
    Graphics();

    bool gl_lib_loaded_;  // To avoid loading the gl library multiple times

    GraphicsFeatures features_;
    GraphicsShadowState shadow_state_;
    bool media_mode_;
    void* gl_context;
    uint32_t active_vertex_attrib_arrays;
    GLuint vao_ = 0;                  // Overgrowth Online: the VAO, and the context it belongs to
    void* vao_context_ = nullptr;
    uint32_t wanted_vertex_attrib_arrays;
    int max_vertex_attrib_arrays;

    const char* shader;

    void SetVertexAttribArrays();

    FullscreenMode::Mode currentFullscreenType;

   public:
    GraphicsConfig config_;
    SDL_Window* sdl_window_;
    bool depth_prepass;
    PostEffects post_effects;
    enum ScreenType { kWindow,
                      kRender,
                      kTexture };

    void Initialize();
    void GetShaderNames(std::map<std::string, int>& preload_shaders);
    void WindowResized(ivec2 value);
    GraphicsFeatures& features() { return features_; }
    void SetMediaMode(bool val) { media_mode_ = val; }
    void SetSeamlessCubemaps(bool val);
    bool media_mode() const { return media_mode_; }
    GLVendor vendor;
    TextureAssetRef noise_ref;
    TextureAssetRef pure_noise_ref;

    int queued_screenshot;
    std::string queued_screenshot_path;  // test bridge: save the next screenshot here
    bool pre_screenshot_media_mode_state;
    enum ScreenshotMode { kGameplay,
                          kTransparentGameplay };
    ScreenshotMode screenshot_mode;

    float shadow_size;

    TextureRef static_shadow_depth_ref;
    TextureRef cascade_shadow_depth_ref;
    TextureRef cascade_shadow_color_ref;
    GLuint cascade_shadow_res;
    RenderTarget cascade_shadow_target;        // (depth only)
    RenderTarget static_shadow_target;         // (depth only)
    RenderTarget cascade_shadow_color_target;  // (colour + the cascades' depth)
    mat4 cascade_shadow_mat[4];
    mat4 simple_shadow_mat;
    float cascade_shadow_radius[4];

    float hdr_white_point;
    float hdr_black_point;
    float hdr_bloom_mult;

    GLuint shadow_res;
    bool drawing_shadow;
    std::stack<ViewportDims> viewport_dims_stack;
    int viewport_dim[4];
    // Overgrowth Online (GL 4.6 stage 5.4): GL's viewport and scissor box as GL has them
    // (set wherever glViewport/glScissor are), and the clear colour: the Vulkan draws
    int gl_viewport_[4] = {0, 0, 0, 0};
    int gl_scissor_[4] = {0, 0, 0, 0};
    float clear_color_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    int vbo_array_bound;
    int vbo_element_bound;

    // Overgrowth Online (GL 4.6 stage 5.0): the scene's targets: colour, velocity and
    // depth; with MSAA, multisample textures resolved into the screen textures
    bool multisample_framebuffer_exists;
    RenderTarget multisample_target;
    TextureRef multisample_color_tex;
    TextureRef multisample_vel_tex;
    TextureRef multisample_depth_tex;

    bool framebuffer_exists;
    RenderTarget scene_target;
    TextureRef screen_color_tex;
    TextureRef screen_vel_tex;
    TextureRef screen_depth_tex;
    std::string post_shader_name;  // Just used for saving

    bool use_sample_alpha_to_coverage;

    int max_texture_units;

    int old_FSAA_samples;
    int old_render_dims[2];
    bool old_post_effects_enable;
    bool first_load;
    bool initialized;
    bool settings_changed;  // for detail objects

    // Overgrowth Online (GL 4.6 stage 5.0): the render target stack (was framebuffer
    // names): each entry keeps how many of its colour attachments were written
    struct RenderTargetState {
        RenderTarget target;
        int draw_count;
    };
    std::stack<RenderTargetState> render_target_stack;
    RenderTarget current_target_;
    GLuint current_fbo_;
    int current_draw_count_;
    bool nav_mesh_out_of_date;
    int nav_mesh_out_of_date_chunk;
    int line_width;
    float point_size_;
    bool wireframe_;
    int max_uniform_block_size_;
    int max_texture_buffer_size_;

    // The following allow us to render the game, UI, and window at different resolutions
    // E.g.
    int window_dims[2];         // dimensions of OS window
    int render_dims[2];         // dimensions of game render target. Same as window_dims if windowed, otherwise internal resolution
    int render_output_dims[2];  // dimensions of render target output in window. Same as window_dims, unless the aspect ratio is off?

    void Clear(const bool clear_color);
    void setViewport(const GLint startx, const GLint starty, const GLint endx, const GLint endy);
    void setAdditiveBlend(const bool what);
    void SetBlendFunc(const int src, const int dest, const int alpha_src = GL_ONE, const int alpha_dst = GL_ONE_MINUS_SRC_ALPHA);
    void setDepthFunc(const int type);
    void setBlend(const bool what);
    void setDepthTest(const bool what);
    void setCullFace(const bool what);
    void setDepthWrite(const bool what);
    void setPolygonOffset(const bool polygon_offset);
    void setAlphaToCoverage(const bool alpha_to_coverage);  // Overgrowth Online (GL 4.6 stage 2.6)
    // Overgrowth Online (GL 4.6 stage 5.8): each menu frame on Vulkan, the recorded
    // pipelines' programs made a few at a time, then their pipelines warmed up
    void VulkanMenuWarmUp();
    void setScissorTest(const bool scissor_test);
    void setColorMask(const bool red, const bool green, const bool blue, const bool alpha);
    void setSimpleShadows(const bool val);
    void setSimpleWater(const bool val);
    void SetParticleFieldSimple(bool val);
    void setDepthOfField(const bool val);
    void setAttribEnvObjInstancing(bool val);

    void PushViewport();
    void PopViewport();

    void TakeScreenshot();

    void setGLState(const GLState& state);
    const GLState& CurrentGLState() const { return shadow_state_.gl_state_; }  // (Overgrowth Online: to restore it)
    bool ScissorTestEnabled() const { return shadow_state_.scissor_test_; }

    void EndTextureSpaceRendering();

    // Overgrowth Online (GL 4.6 stages 2.5, 5.0): render targets. A framebuffer per set
    // of attachments, made once (DSA) and kept; a texture's are dropped when it is
    // deleted (GL hands its name out again). Binding writes all its colour attachments
    void BindRenderTarget(const RenderTarget& target);
    void BindRenderTarget(TextureRef color, int color_level = 0, TextureRef depth = TextureRef(), int depth_level = 0);
    void BindWindow();  // (the window's back buffer)
    const RenderTarget& CurrentRenderTarget() const { return current_target_; }
    void PushRenderTarget();
    void PopRenderTarget();  // (binds the pushed target again, its draw count too)
    // Writes go to the bound target's first `count` colour attachments, the rest are kept
    // (the scene's velocity is written only by what moves)
    void SetDrawBuffers(int count);
    void SetClearColor(const vec4& color);
    // A rectangle of one target's colour attachment (or depth) into another's, scaled
    // to fit; a multisample source is resolved. Scissor plays no part
    void BlitRenderTarget(const RenderTarget& src, int src_color, const PixelRect& src_rect,
                          const RenderTarget& dst, int dst_color, const PixelRect& dst_rect,
                          bool depth = false, bool linear = false);
    void ResolveMultisample();  // the multisample scene into the screen textures
    // Pixels of the bound target's colour attachment (the window: its back buffer)
    void ReadPixels(const PixelRect& rect, GLenum format, GLenum type, void* out, int color = 0);
    void ForgetRenderTargets(GLuint texture_name);

    // Overgrowth Online (GL 4.6 stage 5.0): the rest of what passes did with bare GL
    // - a uniform block's data at its binding point (UBO_*): a region of the frame
    //   ring a UniformRingBuffer filled, or data copied into the ring now
    void BindUniformRing(int binding, const UniformRingBuffer& ring);
    void BindUniformData(int binding, const void* data, size_t size);
    // - device limits, read once
    int MaxUniformBlockSize();
    int MaxTextureBufferSize();
    // - GPU queries: an occlusion query counts the samples its draws pass, a
    //   timestamp marks the GPU's clock; results come a frame or more later
    enum QueryKind { kOcclusionQuery,
                     kTimestampQuery };
    GLuint CreateQuery(QueryKind kind);
    void DeleteQuery(GLuint query);
    void BeginOcclusionQuery(GLuint query);
    void EndOcclusionQuery();
    void WriteTimestamp(GLuint query);
    bool QueryAvailable(GLuint query);
    uint64_t QueryResult(GLuint query);  // (waits when not available)
    // - debug groups (GPU captures and debug output name the passes)
    void PushDebugGroup(const char* name);
    void PopDebugGroup();
    // - state
    void SetWireframe(bool wireframe);
    void SetPointSize(float size);
    void SetScissorRect(int x, int y, int width, int height);  // (setViewport sets it to the viewport)
    void SetViewportOnly(int x, int y, int width, int height);  // (viewport_dim and the scissor box untouched)
    void SetViewportIndexed(int index, int x, int y, int width, int height);  // (and its scissor box: one pass, many viewports)
    void ClearColorOnly();  // the bound target's colour attachments; depth and its write state untouched
    void Flush();
    struct RenderTargetKey {
        GLuint names[RenderTarget::kMaxColor + 1];  // (the colours, then depth; 0: none)
        int levels[RenderTarget::kMaxColor + 1];
        int layers[RenderTarget::kMaxColor + 1];
        bool operator==(const RenderTargetKey& o) const;
    };
    struct CachedFramebuffer {
        RenderTargetKey key;
        GLuint fbo;
        int draw_count;  // (-1: unknown, after a blit)
    };
    std::vector<CachedFramebuffer> render_targets_;
    GLuint FramebufferFor(const RenderTarget& target);

    unsigned int GetShadowSize(unsigned int target_shadow_size);
    void startDraw(vec2 start = vec2(0.0f), vec2 end = vec2(1.0f), ScreenType screen_type = kRender);
    void SwapToScreen();
    void InitScreen();
    void ResizeWindow(int& w, int& h);
    // Overgrowth Online: one log line with the GPU memory the driver reports
    // (AMD: GL_ATI_meminfo, NVIDIA: GL_NVX_gpu_memory_info; others: nothing)
    void LogGPUMemory(const char* when);

    void SetUpWindowDims(int& w, int& h);
    static Graphics* Instance() {
        static Graphics instance;
        return &instance;
    }
    void SetModelMatrix(const mat4& matrix, const mat3& normal_matrix);
    void SetSimpleLineDrawState();
    void BlitDepthBuffer();
    void BlitColorBuffer();

    void SetFromConfig(const Config& config, bool dynamic = false);
    void SetWindowGrab(bool val);
    void BindVBO(int target, int val);
    void UnbindVBO(int target, int val);
    void BindArrayVBO(int val);
    void BindElementVBO(int val);
    void SetLineWidth(int val);
    // void SetFullscreen(bool val);
    void SetFullscreen(FullscreenMode::Mode val);

    void SetFSAA(int val);
    void SetVsync(bool val);
    void SetLimitFpsInGame(bool val);
    void SetMaxFrameRate(int val);
    void SetSimpleFog(bool val);
    void SetDepthOfField(bool val);
    void SetDepthOfFieldReduced(bool val);
    void SetDetailObjects(bool val);
    void SetDetailObjectDecals(bool val);
    void SetDetailObjectLowres(bool val);
    void SetDetailObjectShadows(bool val);
    void SetDetailObjectsReduced(bool val);
    void SetAnisotropy(float val);
    // void SetWindowDimensions( int w, int h );
    void SetResolution(int w, int h, bool force);
    void SetTargetMonitor(int targetMonitor);
    void CheckForWindowResize();
    void StartTextureSpaceRenderingCustom(int x, int y, int width, int height);

    void DrawArrays(GLenum mode, int first, unsigned int count);
    void DrawElements(GLenum mode, unsigned int count, GLenum type, const void* indices);
    void DrawElementsInstanced(GLenum mode, unsigned int count, GLenum type, const void* indices, unsigned int primcount);
    void DrawRangeElements(GLenum mode, unsigned int start, unsigned int end, unsigned int count, GLenum type, const void* indices);

    void EnableVertexAttribArray(unsigned int index);
    void DisableVertexAttribArray(unsigned int index);
    void ResetVertexAttribArrays();
    // Overgrowth Online (GL 4.6 stage 2.4): the layout's VAO for this program,
    // bound, attributes in `enabled` (bit i: the layout's attribute i) on, each
    // reading buffers[i] from offsets[i] with strides[i]; this element buffer.
    // Draw with the usual calls, then EndVertexLayout
    void BeginVertexLayout(VertexLayout& layout, int program, uint32_t enabled, const GLuint* buffers, const GLintptr* offsets, const GLsizei* strides, GLuint element_buffer);
    // The common case: attribute i reads all of vbos[i], tightly packed (floats),
    // where it's valid
    void BeginVertexLayout(VertexLayout& layout, int program, VBOContainer* const* vbos, GLuint element_buffer);
    void EndVertexLayout();
    // (Overgrowth Online stage 5.4: a draw on the Vulkan renderer, with GL's state)
    void VkDrawCall(GLenum mode, unsigned int first, unsigned int count, bool indexed, GLenum type, const void* indices, unsigned int instances);
    bool vertex_layout_bound_ = false;
    GLuint layout_vao_bound_ = 0;
    void ClearGLState();

    void SetUpShadowTextures();

    // output a debug message to OpenGL trace
    void DebugTracePrint(const char* message);
};

bool CheckGLError(int line, const char* file, const char* errstring);
void CheckFBOError(int line, const char* file);

bool CheckGLErrorStr(char* output, unsigned length);

#if GPU_MARKERS
#undef NO_GL_ERROR_CHECKING
#define NO_GL_ERROR_CHECKING 1
#endif  // GPU_MARKERS

#define FORCE_CHECK_GL_ERROR() CheckGLError(__LINE__, __FILE__, NULL)
#define FORCE_CHECK_FBO_ERROR() CheckFBOError(__LINE__, __FILE__)
// Overgrowth Online: the GL debug output's problems (errors, undefined and
// deprecated behaviour): how many, and each message with its count (gl_errors tests)
bool GLDebugOutputAlive();
int GLDebugProblemCount();
std::map<std::string, int> GLDebugProblems();

#ifndef NO_GL_ERROR_CHECKING
#define CHECK_GL_ERROR() CheckGLError(__LINE__, __FILE__, NULL)
#define CHECK_FBO_ERROR() CheckFBOError(__LINE__, __FILE__)
#else
#define CHECK_GL_ERROR()
#define CHECK_FBO_ERROR()
#endif
