//-----------------------------------------------------------------------------
//           Name: vk_ffx.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: FSR 3.1 on the Vulkan renderer, through AMD's FidelityFX API
//                 (built in when CMake's FIDELITYFX_SDK points at the SDK: OG_FSR;
//                 else every call here says no). amd_fidelityfx_vk.dll is loaded
//                 beside the game the first time FSR is asked for; its work is
//                 recorded into the frame's command buffer between the passes, on
//                 the vk_gl textures the temporal pass hands it. Any GPU
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <opengl.h>

#include <cstdint>

namespace VkFfx {

// FSR can run here (the DLL loaded on the first call; why not is logged once)
bool Available();

struct FsrFrame {
    // vk_gl texture names: the frame (HDR), its depth (reversed-Z, infinite far), its
    // motion, where the result goes (a half-float render target: storage usage), and
    // the reactive mask (0: none; drawn before, taa #REACTIVE)
    GLuint color = 0, depth = 0, motion = 0, output = 0;
    GLuint reactive = 0;
    uint32_t render_width = 0, render_height = 0, output_width = 0, output_height = 0;
    float jitter_x = 0.0f, jitter_y = 0.0f;  // pixels, as the projection was offset
    // The motion texture to pixels pointing from this frame to the last one
    float motion_scale_x = 1.0f, motion_scale_y = 1.0f;
    bool reset = false;          // a cut: no history
    float sharpness = 0.0f;      // FSR's own RCAS (0: none)
    float frame_ms = 16.7f;      // since the last frame
    float camera_near = 0.1f;    // the camera's near plane (the far one is at infinity)
    float fov_vertical = 1.0f;   // radians
};
// Recorded now; false: not run (the caller resolves another way)
bool EvaluateFsr(const FsrFrame& frame);

}  // namespace VkFfx
