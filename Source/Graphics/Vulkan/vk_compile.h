//-----------------------------------------------------------------------------
//           Name: vk_compile.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Programs compiled for Vulkan at run time (stage 5.5 of
//                 docs_gl46_plan.md): the ones the offline SPIR-V build doesn't
//                 have (permutations no test run made, mods' shaders). The
//                 engine's own GLSL goes through glslang as spirv_build.py sends
//                 it (relaxed Vulkan rules, bindings and locations mapped, the
//                 stages linked), then gets the same patches (one location per
//                 inter-stage variable by name, the engine's bindings). glslang
//                 is loaded the first time it's needed (glslang.dll beside the
//                 game, else the Vulkan SDK's); the results are kept in the write
//                 directory (Data/VulkanShaders), keyed by the source text
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <string>

namespace VkCompile {

// A program's stages, in VkPrograms' order
enum Stage { kVert,
             kTesc,
             kTese,
             kGeom,
             kFrag,
             kStages };

// The directory with the SPIR-V of the program `name` whose stages are `sources` (the
// engine's assembled GLSL, NULL: no such stage), compiled now if it isn't there yet;
// VkPrograms::Create reads it. Empty: no glslang, or the program doesn't compile (logged)
std::string ProgramDir(const std::string& name, const char* const sources[kStages]);

}  // namespace VkCompile
