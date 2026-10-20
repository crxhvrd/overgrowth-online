//-----------------------------------------------------------------------------
//           Name: reversez.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Reversed-Z helpers (GL 4.6 stage 4a)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <Math/mat4.h>

#include <glm/glm.hpp>

// Overgrowth Online (GL 4.6 stage 4a): depth is reversed. Clip z runs from w at
// the near plane to 0 at the far one (glClipControl ZERO_TO_ONE, Vulkan's range),
// the depth buffer is cleared to 0 and tested GREATER (Graphics::setDepthFunc
// turns the engine's LESS/LEQUAL around). mat4's SetPerspective*/SetOrtho make
// reversed projections; a projection made the GL way (clip z from -w near to +w
// far, glm::ortho and glm::perspective) goes through ReverseZ: z' = (w - z) / 2

inline glm::mat4 ReverseZ(const glm::mat4& gl_proj) {
    glm::mat4 r(1.0f);
    r[2][2] = -0.5f;  // (column, row)
    r[3][2] = 0.5f;
    return r * gl_proj;
}

inline mat4 ReverseZ(const mat4& gl_proj) {
    mat4 r;
    r.LoadIdentity();
    r.entries[10] = -0.5f;  // (column-major)
    r.entries[14] = 0.5f;
    return r * gl_proj;
}
