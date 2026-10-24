//-----------------------------------------------------------------------------
//           Name: temporal.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Temporal anti-aliasing (config "temporal_aa" = 1), on both
//                 renderers. A screen's scene is drawn with its camera jittered
//                 (Halton 2, 3: a different sub-pixel offset each frame); what
//                 moves (characters, items) writes its motion into the scene's
//                 third colour attachment; the rest gets its motion from the
//                 depth and last frame's camera. Before the post effects the
//                 frame is blended with the last one's result, reprojected along
//                 that motion and clipped to the new frame's colours. The motion
//                 and the jitter are what the upscalers read later too
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <Math/mat4.h>
#include <Math/vec2.h>

#include <cstdint>
#include <string>

class Camera;
class VBOContainer;

namespace Temporal {

enum Mode {
    kOff = 0,
    kTAA = 1,
    kDLAA = 2,  // DLSS at the render size (Vulkan, NVIDIA; TAA where it can't run)
    // DLSS from a smaller scene (Graphics::scene_dims) up to the render size; where
    // DLSS can't run, TAA at the render size
    kDLSSQuality = 3,
    kDLSSBalanced = 4,
    kDLSSPerformance = 5,
    kDLSSUltraPerformance = 6,
    // FSR 3.1 the same way (Vulkan, any GPU; TAA at the render size where it can't run)
    kFSRNative = 7,
    kFSRQuality = 8,
    kFSRBalanced = 9,
    kFSRPerformance = 10,
    kFSRUltraPerformance = 11
};

// The setting as Graphics has it (GraphicsConfig::temporal_aa)
int Mode();
bool Enabled();  // a temporal mode is on (MSAA is off then)
// The scene's size over the render size: below 1 when DLSS upscales (it can run here)
float RenderScale();

// Engine::DrawScene, a screen's full draw: the camera jittered for this frame (the
// scene at width x height, the post effects at out_width x out_height). False: no
// temporal pass this frame (off, split screen, no scene target)
bool BeginScene(Camera* camera, int width, int height, int out_width, int out_height);
// ... after the scene, before the post effects: the camera unjittered again
void EndScene(Camera* camera);
// Between the two: what moves writes its motion (Graphics::SetDrawBuffers(3))
bool InScene();
// SceneGraph::Draw, the opaque part drawn (transparent things and particles to come):
// FSR's reactive mask is made from what's drawn after this
void OpaqueDrawn();
// A program suffix (the pass's global one) for what writes its motion: #TEMPORAL_MV
// added, #NO_VELOCITY_BUF taken out (the motion goes out after the velocity, location 2)
std::string MotionSuffix(const char* global_suffix);

// The scene's frame (the scene target's textures) resolved into Graphics::PostColor(),
// before the post effects (ApplyPostEffects). Draws full-screen quads with the post
// effects' vertex buffers. False: no scene began (nothing written)
bool Resolve(const VBOContainer& quad_vert_vbo, const VBOContainer& quad_index_vbo);

// This frame's camera without the jitter, and last frame's (the same on the first
// frame after a reset)
const mat4& ViewProj();
const mat4& PrevViewProj();
// The jitter this frame, in pixels of the scene's render target ([-0.5, 0.5])
vec2 JitterPixels();
// Frames resolved so far: what an object keeps from the frame before is this
// number minus one
uint64_t Frame();
// The history isn't blended in on the next frame (a cut, a level load, a resize)
void Reset();

}  // namespace Temporal
