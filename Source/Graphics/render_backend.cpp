//-----------------------------------------------------------------------------
//           Name: render_backend.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See render_backend.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "render_backend.h"

#include <atomic>

RenderBackend g_render_backend = RenderBackend::kGL;

namespace VkStub {

unsigned NewId() {
    static std::atomic<unsigned> next(1);
    return next++;
}

}  // namespace VkStub
