//-----------------------------------------------------------------------------
//           Name: vboringcontainer.h
//      Developer: Wolfire Games LLC
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

#include <Internal/referencecounter.h>
#include <Internal/integer.h>

#include <Graphics/vboenums.h>

#include <opengl.h>

#ifndef V_MIBIBYTE
#define V_MIBIBYTE 1024 * 1024
#endif

#ifndef V_KIBIBYTE
#define V_KIBIBYTE 1024
#endif

// Overgrowth Online (GL 4.6 stage 2.3): the frame ring. One persistently mapped
// buffer that per-frame data (uniform batches, streamed vertices) is written
// straight into, instead of buffers re-specified ("orphaned") each time they
// fill up. A fence ends each frame, and a region is written again only once the
// GPU is done with the frame that wrote it. What's written is for the frame it
// was written in: a later frame writes it again
namespace FrameRing {
struct Allocation {
    GLuint buffer;
    GLuint offset;
    void* ptr;
};
Allocation Allocate(size_t size, size_t alignment);
void EndFrame();  // after the swap: fences this frame's writes
uint64_t Frame();  // the current frame (frames ended so far)
// Overgrowth Online (GL 4.6 stage 4a): an RGBA32F buffer texture over the whole
// ring (made again when the ring grows): a shader reads what a draw wrote at
// texel offset / 16 (GPU skinning's bones). Call after the writes it's for
GLuint BufferTexture();
}  // namespace FrameRing

class VBORingContainer {
    int gl_VBO;
    char flags;
    bool element;
    bool force_reload;

    GLuint current_offset;
    GLuint next_offset;
    // Used space (requested by last loaded buffer) plus alignment padding
    GLuint allocated_size;
    // Amount of space requested to use for last filled buffer
    GLuint used_size;

    // Total size of entire ring buffer
    GLuint storage_size;
    GLuint storage_size_hint;

    GLenum target;
    GLenum hint;

    int storage_multiplier;

    // Overgrowth Online (GL 4.6 stage 2.3): kVBOFrame: each fill goes into the frame
    // ring (gl_VBO is the ring's buffer), for draws in the frame it was filled in
    bool frame_ring;
    uint64_t fill_frame;
    bool warned_stale;

   public:
    void Dispose();
    void SetHint(GLuint storage_size, char flags, bool ignore_multiplier = false);
    void Fill(GLuint size, void* data);
    void Bind() const;
    bool valid() const;
    unsigned size();  // returns size in bytes.
    uintptr_t offset() const;
    GLuint id() const;  // Overgrowth Online: the buffer's name (for a layout's binding)

    VBORingContainer(GLuint storage_size, char flags, bool ignore_multiplier = false);
    ~VBORingContainer();

   private:
    VBORingContainer(const VBORingContainer& other);
    VBORingContainer& operator=(const VBORingContainer& other);
};

typedef ReferenceCounter<VBORingContainer> RC_VBORingContainer;

// Overgrowth Online (GL 4.6 stage 2.3): written into the frame ring. gl_id,
// offset and next_offset are the last fill's range (bind it with them)
class UniformRingBuffer {
   public:
    int size;
    int gl_id;
    int offset;
    int next_offset;
    uint64_t fill_frame;  // FrameRing::Frame() of the last fill

    UniformRingBuffer() : gl_id(-1),
                          size(0),
                          offset(0),
                          next_offset(0),
                          fill_frame(~0ull) {}

    void Create(int desired_size);
    void Fill(int data_size, void* data);
    bool FilledThisFrame() const { return fill_frame == FrameRing::Frame(); }
};
