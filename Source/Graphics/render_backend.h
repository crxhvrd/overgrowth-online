//-----------------------------------------------------------------------------
//           Name: render_backend.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Which renderer runs: OpenGL 4.6 or Vulkan 1.3 (config
//                 "renderer" = gl | vulkan; stage 5 of docs_gl46_plan.md).
//                 Graphics, Textures, Shaders, the VBO containers and the frame
//                 ring branch on UseVulkan(); the Vulkan side is in
//                 Graphics/Vulkan. Textures, buffers, samplers, fences and limits
//                 go through their GL-shaped twins there (vk_gl.h, stage 5.3).
//                 Until the rest has its Vulkan implementation (stages 5.4-5.6)
//                 its GL entry points take a stub branch: the engine's own
//                 bookkeeping runs, nothing reaches a GPU
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

enum class RenderBackend {
    kGL,
    kVulkan
};

// Set once, before the window is made (Graphics::InitScreen); a Vulkan start that
// fails falls back to GL there
extern RenderBackend g_render_backend;

inline bool UseVulkan() { return g_render_backend == RenderBackend::kVulkan; }

namespace VkStub {

// A GL-style name, never 0, never repeated: the vk_gl.h twins' objects, and a fake
// one for an object GL would have made that has no Vulkan side yet (program, shader,
// framebuffer, vertex array, query), so the engine's "made yet?" checks hold
unsigned NewId();

}  // namespace VkStub
