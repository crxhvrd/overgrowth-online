//-----------------------------------------------------------------------------
//           Name: vk_draw.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Drawing on the Vulkan renderer (stages 5.4-5.5 of
//                 docs_gl46_plan.md). Graphics tells it the render target, the
//                 vertex input and the uniform blocks as it binds them; each
//                 draw brings GL's state (blend, depth, cull, masks, viewport,
//                 scissor), from which a pipeline is found or made (Vulkan 1.3
//                 dynamic state keeps most of it out of the key), the program's
//                 textures and blocks are pushed as descriptors and the draw is
//                 recorded inside dynamic rendering, begun at the first draw or
//                 clear and ended before any transfer. Images keep GL's memory
//                 layout: the window's frame is flipped once, at the present
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <opengl.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VkDraw {

// ---- render targets
struct TargetAttachment {
    GLuint texture = 0;  // GL-style name (vk_gl); 0: none
    int level = 0;
    int layer = -1;  // -1: the texture itself
    bool operator==(const TargetAttachment& o) const { return texture == o.texture && level == o.level && layer == o.layer; }
};
struct Target {
    static const int kMaxColor = 4;
    TargetAttachment color[kMaxColor];
    int num_color = 0;
    TargetAttachment depth;
    bool IsWindow() const { return num_color == 0 && depth.texture == 0; }
};
void SetTarget(const Target& target);  // (the same one again: nothing ends)
void SetDrawCount(int count);          // GL's draw buffers: colour attachments written (-1: all)
// GL's glClear: the scissor box when its test is on
void Clear(bool color, bool depth, const float rgba[4], bool scissor_test, const int scissor[4]);

// ---- vertex input (Graphics::BeginVertexLayout)
struct VertexAttrib {
    uint32_t location = 0;
    uint32_t binding = 0;
    GLint size = 4;
    GLenum type = GL_FLOAT;
    bool normalized = false;
    uint32_t offset = 0;
};
struct VertexInput {
    static const int kMaxBindings = 15;  // (16th: the stand-in for inputs nothing feeds)
    std::vector<VertexAttrib> attribs;   // the ones on
    GLuint buffers[kMaxBindings] = {};
    uint64_t offsets[kMaxBindings] = {};
    uint64_t strides[kMaxBindings] = {};
    uint32_t divisors[kMaxBindings] = {};
    GLuint element_buffer = 0;
};
void SetVertexInput(const VertexInput& input);
void ClearVertexInput();

// ---- uniform blocks (binding B: GL's uniform buffer binding point)
void SetUniformBuffer(uint32_t binding, GLuint buffer, size_t offset, size_t size);

// ---- draws
struct State {
    bool blend = false;
    GLenum blend_src = GL_ONE, blend_dst = GL_ZERO, blend_alpha_src = GL_ONE, blend_alpha_dst = GL_ZERO;
    bool depth_test = false, depth_write = true;
    GLenum depth_func = GL_LESS;  // (as GL is told: reversed-Z already applied)
    bool cull = false;
    bool polygon_offset = false;
    bool alpha_to_coverage = false;
    uint8_t color_mask = 0xF;
    bool wireframe = false;
    float line_width = 1.0f;
    bool scissor_test = false;
    int scissor[4] = {0, 0, 0, 0};
    int viewport[4] = {0, 0, 0, 0};
};
// program: the bound program's GL-style name (VkPrograms)
void Draw(const State& state, GLuint program, GLenum mode, uint32_t first, uint32_t count, bool indexed, GLenum index_type,
          size_t index_offset, uint32_t instances);

// ---- the window
void Present();
// GL's glReadPixels from the current target (color: its attachment index; a depth
// format reads the depth attachment)
bool ReadPixels(int x, int y, int w, int h, GLenum format, GLenum type, void* out, size_t out_size, int color);
// The window's colour texture (a vk_gl name; made at the drawable's size)
GLuint WindowColor();

// Graphics::BlitRenderTarget between two targets' attachments (rects x0, y0, x1, y1)
void Blit(const Target& src, int src_color, const int src_rect[4], const Target& dst, int dst_color, const int dst_rect[4], bool depth,
          bool linear);

struct Stats {
    uint64_t draws = 0, pipelines = 0, passes = 0;
    uint64_t skipped = 0;  // draws with no program, layout or target
};
Stats GetStats();

}  // namespace VkDraw
