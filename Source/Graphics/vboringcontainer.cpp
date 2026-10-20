//-----------------------------------------------------------------------------
//           Name: vboringcontainer.cpp
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
#include "vboringcontainer.h"

#include <Graphics/graphics.h>
#include <Logging/logdata.h>
#include <Utility/assert.h>
#include <Internal/profiler.h>

#include <SDL_assert.h>

#include <deque>
#include <vector>
#include <cstring>

const unsigned kAlignPadding = 64;

const unsigned kBufferSizeMultiplier = 32;

static int buffer_size_multipler_offset = 0;

VBORingContainer::VBORingContainer(GLuint _storage_size, char flags, bool ignore_multiplier) : used_size(0),
                                                                                               allocated_size(0),
                                                                                               gl_VBO(-1),
                                                                                               storage_size(0),
                                                                                               hint(0),
                                                                                               storage_multiplier(kBufferSizeMultiplier + buffer_size_multipler_offset++ % 32) {
    SetHint(_storage_size, flags & (kVBOStatic | kVBODynamic | kVBOStream), ignore_multiplier);
    next_offset = storage_size;

    if (flags & kVBOElement) {
        target = GL_ELEMENT_ARRAY_BUFFER;
    } else if (flags & kVBOFloat) {
        target = GL_ARRAY_BUFFER;
    } else {
        target = 0xFFFFFFFF;
        // No target flag set
        SDL_assert(false);
    }

    element = flags & kVBOElement;
    force_reload = flags & kVBOForceReBufferData;
    frame_ring = (flags & kVBOFrame) != 0;
    fill_frame = ~0ull;
    warned_stale = false;
}

void VBORingContainer::SetHint(GLuint storage, char flags, bool ignore_multiplier) {
    LOG_ASSERT(((flags & ~(kVBOStatic | kVBODynamic | kVBOStream)) == 0));
    GLenum old_hint = hint;
    if (flags & kVBOStatic) {
        hint = GL_STATIC_DRAW;
    } else if (flags & kVBODynamic) {
        hint = GL_DYNAMIC_DRAW;
    } else if (flags & kVBOStream) {
        hint = GL_STREAM_DRAW;
    } else {
        hint = 0xFFFFFFFF;
        // No hint flag set
        SDL_assert(false);
    }
    // Indicate we want to reallocate the buffer;
    if (old_hint != hint)
        next_offset = storage_size;

    if (storage % kAlignPadding) {
        storage = storage + kAlignPadding - storage % kAlignPadding;
    }

    if (hint == GL_STATIC_DRAW) {
        storage_size_hint = storage;
    } else {
        storage_size_hint = storage * (ignore_multiplier ? 1 : storage_multiplier);
    }
}

void VBORingContainer::Fill(GLuint size, void* data) {
    if (size > 0 && frame_ring) {
        LOG_ASSERT(data);
        used_size = size;
        allocated_size = size;
        if (allocated_size % kAlignPadding) {
            allocated_size = allocated_size + kAlignPadding - allocated_size % kAlignPadding;
        }
        FrameRing::Allocation a = FrameRing::Allocate(allocated_size, kAlignPadding);
        {
            PROFILER_ZONE(g_profiler_ctx, "memcpy");
            memcpy(a.ptr, data, size);
        }
        gl_VBO = a.buffer;
        current_offset = a.offset;
        fill_frame = FrameRing::Frame();
        return;
    }
    if (size > 0) {
        Graphics* graphics = Graphics::Instance();
        int old_gl_VBO = gl_VBO;
        if (gl_VBO == -1) {
            GLuint val;
            glGenBuffers(1, &val);
            gl_VBO = val;
        }

        used_size = size;
        // pad utilized size to ensure maxixum use of bus.
        allocated_size = size;
        if (allocated_size % kAlignPadding) {
            allocated_size = allocated_size + kAlignPadding - allocated_size % kAlignPadding;
        }

        LOG_ASSERT(data);

        while (allocated_size > storage_size) {
            if (storage_size == storage_size_hint) {
                LOGW << "Requested size is larger than internal preallocated size, resizing to a larger buffer." << std::endl;
                storage_size_hint = storage_size_hint * 4;
            }

            storage_size = storage_size_hint;
            next_offset = storage_size;
        }

        graphics->BindVBO(target, gl_VBO);

        // if our next size is too big to fit in the remainder of the buffer, create a new one.
        if (next_offset + allocated_size > storage_size) {
            if (old_gl_VBO != -1) {
                PROFILER_ENTER(g_profiler_ctx, "Orphan VBORingContainer buffer");
            } else {
                PROFILER_ENTER(g_profiler_ctx, "Create VBORingContainer buffer");
            }
            storage_size = storage_size_hint;
            glBufferData(target, storage_size, NULL, hint);
            next_offset = 0;
            PROFILER_LEAVE(g_profiler_ctx);
        }

        const bool kUseMapBufferRange = false;
        if (kUseMapBufferRange) {
            void* mapped = glMapBufferRange(target, next_offset, used_size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            memcpy(mapped, data, used_size);
            glUnmapBuffer(target);
        } else {
            PROFILER_ZONE(g_profiler_ctx, "glBufferSubData");
            glBufferSubData(target, next_offset, used_size, data);
        }

        current_offset = next_offset;
        next_offset = next_offset + allocated_size;

        graphics->BindVBO(target, 0);
    }
}

void VBORingContainer::Dispose() {
    if (valid() == false) {
        return;
    }
    if (frame_ring) {  // (the ring's buffer isn't its own)
        gl_VBO = -1;
        current_offset = 0;
        allocated_size = 0;
        used_size = 0;
        return;
    }
    Graphics* graphics = Graphics::Instance();

    GLenum target;
    if (element) {
        target = GL_ELEMENT_ARRAY_BUFFER;
    } else {
        target = GL_ARRAY_BUFFER;
    }

    // LOGI << "Disposing vbo: " << gl_VBO << " for target " << target << std::endl;
    LogSystem::Flush();
    graphics->UnbindVBO(target, gl_VBO);
    GLuint val = gl_VBO;
    glDeleteBuffers(1, &val);
    gl_VBO = -1;
    next_offset = storage_size;
    current_offset = 0;
    allocated_size = 0;
    used_size = 0;
}

void VBORingContainer::Bind() const {
    if (valid() == false) {
        LOG_ASSERT(false);
        return;
    }
    if (frame_ring && fill_frame != FrameRing::Frame() && !warned_stale) {
        LOGE << "A frame-ring vertex buffer drawn in a later frame than its fill (it needs kVBOFrame removed)" << std::endl;
        const_cast<VBORingContainer*>(this)->warned_stale = true;
    }

    if (element) {
        Graphics::Instance()->BindElementVBO(gl_VBO);
    } else {
        Graphics::Instance()->BindArrayVBO(gl_VBO);
    }
}

GLuint VBORingContainer::id() const {
    if (frame_ring && fill_frame != FrameRing::Frame() && !warned_stale) {
        LOGE << "A frame-ring vertex buffer drawn in a later frame than its fill (it needs kVBOFrame removed)" << std::endl;
        const_cast<VBORingContainer*>(this)->warned_stale = true;
    }
    return (GLuint)gl_VBO;
}

bool VBORingContainer::valid() const {
    return gl_VBO != -1;
}

unsigned VBORingContainer::size() {
    return valid() ? used_size : 0;
}

uintptr_t VBORingContainer::offset() const {
    if (valid()) {
        return (uintptr_t)(current_offset);
    } else {
        return 0;
    }
}

VBORingContainer::~VBORingContainer() {
    Dispose();
}

namespace {
// The frame ring (vboringcontainer.h). Live data runs from the oldest frame the
// GPU may still read (ring_marks.front().start, or this frame's start) to
// ring_head, wrapping at the end; writes never catch up with it
struct FrameMark {
    uint64_t frame;
    size_t start;
    GLsync fence;
};
const size_t kFrameRingInitialSize = 32 * V_MIBIBYTE;
const size_t kFrameRingMaxSize = 256 * V_MIBIBYTE;
GLuint ring_buffer = 0;
char* ring_ptr = NULL;
size_t ring_size = 0;
size_t ring_head = 0;         // where the next write goes
size_t ring_frame_start = 0;  // where this frame's writes start
uint64_t ring_frame = 0;
std::deque<FrameMark> ring_marks;      // earlier frames the GPU may still read, oldest first
std::vector<GLuint> ring_retired;      // outgrown this frame (its draws still use them)

void CreateRing(size_t size) {
    if (ring_buffer != 0) {
        ring_retired.push_back(ring_buffer);
        for (FrameMark& m : ring_marks) {
            glDeleteSync(m.fence);
        }
        ring_marks.clear();
        LOGI << "Frame ring grown to " << size / V_MIBIBYTE << " MiB" << std::endl;
    }
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glCreateBuffers(1, &ring_buffer);
    glNamedBufferStorage(ring_buffer, size, NULL, flags);
    ring_ptr = (char*)glMapNamedBufferRange(ring_buffer, 0, size, flags);
    if (ring_ptr == NULL) {
        FatalError("Error", "Could not map the frame ring buffer");
    }
    ring_size = size;
    ring_head = 0;
    ring_frame_start = 0;
}

// The oldest earlier frame's region becomes free (waiting for the GPU to finish
// that frame if it hasn't). False: no earlier frame holds any
bool ReleaseOldest() {
    if (ring_marks.empty()) {
        return false;
    }
    FrameMark& m = ring_marks.front();
    if (glClientWaitSync(m.fence, 0, 0) == GL_TIMEOUT_EXPIRED) {
        PROFILER_ZONE_STALL(g_profiler_ctx, "Frame ring: wait for the GPU");
        while (glClientWaitSync(m.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull) == GL_TIMEOUT_EXPIRED) {
        }
    }
    glDeleteSync(m.fence);
    ring_marks.pop_front();
    return true;
}

// This frame's writes so far as a frame of their own (the ring is at its largest
// and one frame fills it: the next write waits for these)
void SplitFrame() {
    ring_marks.push_back({ring_frame, ring_frame_start, glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)});
    ring_frame_start = ring_head;
}
}  // namespace

FrameRing::Allocation FrameRing::Allocate(size_t size, size_t alignment) {
    if (ring_buffer == 0) {
        CreateRing(kFrameRingInitialSize);
    }
    while (true) {
        const bool empty = ring_marks.empty() && ring_head == ring_frame_start;
        const size_t tail = ring_marks.empty() ? ring_frame_start : ring_marks.front().start;
        size_t at = (ring_head + alignment - 1) / alignment * alignment;
        bool fits;
        if (empty) {
            if (at + size > ring_size) {
                at = 0;
            }
            fits = at + size <= ring_size;
        } else if (ring_head >= tail) {  // live: [tail, head)
            fits = at + size <= ring_size;
            if (!fits && size < tail) {
                at = 0;
                fits = true;
            }
        } else {  // live: [tail, end) and [0, head)
            fits = at + size < tail;
        }
        if (fits) {
            if (empty) {
                ring_frame_start = at;  // (nothing in use: this frame starts where it writes)
            }
            ring_head = at + size;
            Allocation a;
            a.buffer = ring_buffer;
            a.offset = (GLuint)at;
            a.ptr = ring_ptr + at;
            return a;
        }
        if (ReleaseOldest()) {
            continue;
        }
        // This frame alone fills the ring: a larger one, or (at the largest) wait
        // for this frame's earlier writes
        if (ring_size < kFrameRingMaxSize || size > ring_size / 2) {
            size_t new_size = ring_size * 2;
            while (new_size < size * 4) {
                new_size *= 2;
            }
            CreateRing(new_size);
        } else {
            SplitFrame();
        }
    }
}

void FrameRing::EndFrame() {
    if (ring_buffer != 0 && ring_head != ring_frame_start) {
        SplitFrame();
    }
    for (GLuint id : ring_retired) {
        // (the GPU's pending reads keep it until they're done; the bind cache forgets
        // it, as GL may hand the name out again)
        Graphics::Instance()->UnbindVBO(GL_ARRAY_BUFFER, id);
        Graphics::Instance()->UnbindVBO(GL_ELEMENT_ARRAY_BUFFER, id);
        glDeleteBuffers(1, &id);
    }
    ring_retired.clear();
    ++ring_frame;
}

uint64_t FrameRing::Frame() {
    return ring_frame;
}

GLuint FrameRing::BufferTexture() {
    static GLuint texture = 0;
    static GLuint texture_buffer = 0;
    if (texture != 0 && texture_buffer == ring_buffer) {
        return texture;
    }
    if (texture != 0) {
        glDeleteTextures(1, &texture);  // (the ring grew: a new buffer)
    }
    glCreateTextures(GL_TEXTURE_BUFFER, 1, &texture);
    glTextureBuffer(texture, GL_RGBA32F, ring_buffer);
    texture_buffer = ring_buffer;
    return texture;
}

static GLint max_ubo_size = -1;
static GLint ubo_alignment = -1;

void UniformRingBuffer::Create(int desired_size) {
    if (max_ubo_size == -1) {
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_ubo_size);
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_alignment);
    }
    size = desired_size;  // (the most one fill may write)
    gl_id = 0;
    offset = 0;
    next_offset = 0;
}

void UniformRingBuffer::Fill(int data_size, void* data) {
    if (data_size > size || data_size > max_ubo_size) {
        FatalError("Error", "Data is too big for uniform ring buffer");
    }
    // Overgrowth Online (GL 4.6 stage 2.3): into the frame ring; the range bound
    // is the size rounded up to the alignment, as before
    int aligned_size = ((data_size + (ubo_alignment - 1)) / ubo_alignment) * ubo_alignment;
    FrameRing::Allocation a = FrameRing::Allocate(aligned_size, ubo_alignment);
    {
        PROFILER_ZONE(g_profiler_ctx, "memcpy");
        memcpy(a.ptr, data, data_size);
    }
    gl_id = a.buffer;
    offset = a.offset;
    next_offset = offset + aligned_size;
    fill_frame = FrameRing::Frame();
}
