//-----------------------------------------------------------------------------
//           Name: vk_programs.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Programs on the Vulkan renderer (stage 5.4 of
//                 docs_gl46_plan.md): each engine program's stages from the
//                 offline SPIR-V build (stage 3, config "shader_vulkan_glsl"),
//                 read with SPIRV-Reflect. Its textures (unit N) and uniform
//                 blocks (binding B, as 64 + B) go into one push-descriptor set;
//                 the reflection fills the engine's Program as GL's link did:
//                 the default block's members (SetUniform* write them), the
//                 texture units it samples, its attribute locations by name
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <volk.h>

#include <opengl.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Program;

namespace VkPrograms {

const uint32_t kBlockBindingBase = 64;  // uniform block B at binding 64 + B (textures: their unit)

struct Binding {
    uint32_t binding = 0;  // in the push set
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uint32_t unit = 0;        // the texture unit, or the GL block binding
    uint32_t block_size = 0;  // uniform blocks: the bytes the shaders read
    VkImageViewType view = VK_IMAGE_VIEW_TYPE_2D;  // what the shader samples (for a stand-in when nothing is bound)
    bool multisampled = false;
    bool shadow = false;  // a depth comparison sampler
    bool integer = false;  // an integer texture (isampler/usampler)
    bool used = false;  // a stage reads it (declared-only ones stay in the layout, as GL's dead ones)
};

struct Input {
    std::string name;
    uint32_t location = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;  // the shader's type
};

// A uniform block, as GL's block queries see it (the engine packs instance data by these)
struct Block {
    std::string name;     // its type name ("InstanceInfo")
    uint32_t unit = 0;    // GL block binding
    uint32_t size = 0;
    std::vector<std::pair<std::string, uint32_t> > members;  // GL's names ("transforms[0]", "InstanceInfo.transforms[0]") -> offset
};

struct Info {
    std::string name;
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<Binding> bindings;
    std::vector<Block> blocks;
    std::vector<Input> inputs;  // the vertex stage's
    uint32_t color_outputs = 0;  // the fragment stage writes locations [0, n)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    bool tessellation = false;
    uint64_t id = 0;  // (pipeline keys)
    uint32_t flags = 0;  // the engine's optional stages (Shaders::OptionalShaders)
};

// The program `name` (its GL-style name) from the build's directory for it; fills
// program as above. False: logged, nothing to draw with
bool Create(GLuint name, const std::string& dir, Program& program);
void Destroy(GLuint name);
// Runs before a program is destroyed (pipelines being made from it on other threads stop)
void OnBeforeDestroy(std::function<void()> before_destroy);
const Info* Get(GLuint name);
// GL's block queries (-1 / GL_INVALID_INDEX: none): a block's index by its name, its
// size; a member's index by its GL name, its offset
int BlockIndex(GLuint program, const char* block);
int BlockSize(GLuint program, int index);
int MemberIndex(GLuint program, const std::string& member);
int MemberOffset(GLuint program, int index);
size_t Count();
// Every program made (the pipelines made ahead find theirs by name)
void ForEach(const std::function<void(const Info&)>& fn);

}  // namespace VkPrograms
