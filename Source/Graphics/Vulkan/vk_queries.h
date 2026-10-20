//-----------------------------------------------------------------------------
//           Name: vk_queries.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: GL's queries on the Vulkan renderer (stage 5.6 of
//                 docs_gl46_plan.md): occlusion queries (samples passed: the
//                 flares' visibility, characters hidden behind things) and
//                 timestamps (the frame timers), by GL-style name. Each use takes
//                 a slot from the frame's share of a query pool, reset as the
//                 frame begins. An occlusion query counts only inside passes; one
//                 that spans a pass break has a slot per pass, summed. Results
//                 are read once their submit has finished
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <opengl.h>

#include <cstdint>

namespace VkQueries {

GLuint Create(bool timestamp);
void Delete(GLuint query);
void BeginOcclusion(GLuint query);
void EndOcclusion();
void WriteTimestamp(GLuint query);  // (nanoseconds, as GL_TIMESTAMP)
bool Available(GLuint query);
uint64_t Result(GLuint query);  // (waits for it)

// For the passes (vk_draw.cpp): one has begun, or is about to end
void OnPassBegin();
void OnPassEnd();

}  // namespace VkQueries
