//-----------------------------------------------------------------------------
//           Name: vk_ngx.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: DLSS on the Vulkan renderer, through NVIDIA's NGX SDK (built in
//                 when CMake's DLSS_SDK points at it: OG_DLSS; else every call here
//                 says no). NGX's instance and device extensions are asked for when
//                 they're made; NGX itself starts the first time DLSS is asked for.
//                 Its work is recorded into the frame's command buffer between the
//                 passes, on the vk_gl textures the temporal pass hands it
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <opengl.h>

#include <cstdint>
#include <vector>

namespace VkNgx {

#ifdef VK_DEFINE_HANDLE
// Before vkCreateInstance / vkCreateDevice: what NGX needs, of what the PC has, added
// once each
void AppendInstanceExtensions(std::vector<const char*>* extensions);
void AppendDeviceExtensions(VkInstance instance, VkPhysicalDevice physical, const std::vector<VkExtensionProperties>& available,
                            std::vector<const char*>* extensions);
#endif

// The settings ask for DLSS (config temporal_aa: DLAA or a DLSS mode): the instance is made for it
bool Requested();
// DLSS can run on this PC (NGX started on the first call; why not is logged once)
bool DlssAvailable();

struct DlssFrame {
    // vk_gl texture names: the frame (HDR), its depth (reversed-Z), its motion, and where
    // the result goes (a half-float render target: storage usage)
    GLuint color = 0, depth = 0, motion = 0, output = 0;
    uint32_t render_width = 0, render_height = 0, output_width = 0, output_height = 0;
    float jitter_x = 0.0f, jitter_y = 0.0f;  // pixels, as the projection was offset
    // The motion texture to pixels pointing from this frame to the last one
    float motion_scale_x = 1.0f, motion_scale_y = 1.0f;
    bool reset = false;  // a cut: no history
    int quality = 0;     // NVSDK_NGX_PerfQuality_Value (DLAA: render size = output size)
    int preset = 0;      // NVSDK_NGX_DLSS_Hint_Render_Preset (0: NGX's default)
};
// Recorded now; false: not run (the caller resolves another way)
bool EvaluateDlss(const DlssFrame& frame);

}  // namespace VkNgx
