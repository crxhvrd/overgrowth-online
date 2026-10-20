//-----------------------------------------------------------------------------
//           Name: vk_programs.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_programs.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "vk_programs.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/shaders.h>
#include <Logging/logdata.h>
#include <Internal/config.h>
#include <Compat/fileio.h>

#include <spirv_reflect.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <unordered_map>

namespace VkPrograms {
namespace {

std::unordered_map<GLuint, std::unique_ptr<Info> >& programs = *new std::unordered_map<GLuint, std::unique_ptr<Info> >();  // (never destroyed: see vk_gl.cpp)
bool shut_down = false;
uint64_t next_id = 1;

struct StageFile {
    const char* file;
    VkShaderStageFlagBits stage;
};
const StageFile kStages[] = {
    {"vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
    {"tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {"tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
    {"geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
    {"frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT},
};

// (my_fopen: the run-time compiled ones are in the write directory, whose path may not be ASCII)
bool ReadFile(const std::string& path, std::vector<uint32_t>& words) {
    FILE* file = my_fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    std::vector<char> bytes;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
        bytes.insert(bytes.end(), buf, buf + n);
    }
    fclose(file);
    if (bytes.empty() || bytes.size() % 4 != 0) {
        return false;
    }
    words.resize(bytes.size() / 4);
    memcpy(words.data(), bytes.data(), bytes.size());
    return true;
}

// The GL type a default-block member had (what Shaders::ShadowWrite checks setters
// against). Bools are uints in a Vulkan uniform block: the shaders have no uint
// uniforms, so a uint is a bool
GLenum GLTypeOf(const SpvReflectTypeDescription* t, bool* is_bool) {
    *is_bool = false;
    if (t == NULL) {
        return GL_NONE;
    }
    const SpvReflectTypeFlags f = t->type_flags;
    const SpvReflectNumericTraits& n = t->traits.numeric;
    if (f & SPV_REFLECT_TYPE_FLAG_MATRIX) {
        const uint32_t c = n.matrix.column_count, r = n.matrix.row_count;
        if (c == 2 && r == 2) return GL_FLOAT_MAT2;
        if (c == 3 && r == 3) return GL_FLOAT_MAT3;
        if (c == 4 && r == 4) return GL_FLOAT_MAT4;
        if (c == 2 && r == 3) return GL_FLOAT_MAT2x3;
        if (c == 2 && r == 4) return GL_FLOAT_MAT2x4;
        if (c == 3 && r == 2) return GL_FLOAT_MAT3x2;
        if (c == 3 && r == 4) return GL_FLOAT_MAT3x4;
        if (c == 4 && r == 2) return GL_FLOAT_MAT4x2;
        if (c == 4 && r == 3) return GL_FLOAT_MAT4x3;
        return GL_NONE;
    }
    const uint32_t comps = (f & SPV_REFLECT_TYPE_FLAG_VECTOR) ? n.vector.component_count : 1;
    if (comps < 1 || comps > 4) {
        return GL_NONE;
    }
    if (f & SPV_REFLECT_TYPE_FLAG_FLOAT) {
        const GLenum k[] = {GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};
        return k[comps - 1];
    }
    if ((f & SPV_REFLECT_TYPE_FLAG_BOOL) || ((f & SPV_REFLECT_TYPE_FLAG_INT) && n.scalar.signedness == 0)) {
        *is_bool = true;
        const GLenum k[] = {GL_BOOL, GL_BOOL_VEC2, GL_BOOL_VEC3, GL_BOOL_VEC4};
        return k[comps - 1];
    }
    if (f & SPV_REFLECT_TYPE_FLAG_INT) {
        const GLenum k[] = {GL_INT, GL_INT_VEC2, GL_INT_VEC3, GL_INT_VEC4};
        return k[comps - 1];
    }
    return GL_NONE;
}

VkImageViewType ViewTypeOf(const SpvReflectImageTraits& image) {
    switch (image.dim) {
        case SpvDim1D: return image.arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        case SpvDim3D: return VK_IMAGE_VIEW_TYPE_3D;
        case SpvDimCube: return image.arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        default: return image.arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    }
}

struct Member {
    GLint offset, array_stride, matrix_stride, size;
    GLenum type;
    bool is_bool;
};

}  // namespace

void DestroyAll() {
    std::vector<GLuint> names;
    for (const auto& kv : programs) {
        names.push_back(kv.first);
    }
    for (GLuint n : names) {
        Destroy(n);
    }
    shut_down = true;
}

bool Create(GLuint name, const std::string& dir, Program& program) {
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkBackend::OnShutdown(&DestroyAll);
    }
    Destroy(name);
    std::unique_ptr<Info> info(new Info());
    info->name = program.name;
    info->flags = (uint32_t)program.optional_shaders;
    info->id = next_id++;
    std::map<uint32_t, Binding> bindings;
    std::map<uint32_t, VkShaderStageFlags> binding_stages;
    std::map<std::string, Member> members;  // the default block's
    uint32_t default_block_size = 0;
    bool ok = true;
    VkDevice device = VkBackend::Device();

    for (const StageFile& sf : kStages) {
        std::vector<uint32_t> code;
        if (!ReadFile(dir + "/" + sf.file, code)) {
            if (sf.stage == VK_SHADER_STAGE_VERTEX_BIT || sf.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
                LOGE << "Vulkan: " << program.name << " has no " << sf.file << " in " << dir << std::endl;
                ok = false;
            }
            continue;
        }
        SpvReflectShaderModule module;
        if (spvReflectCreateShaderModule(code.size() * 4, code.data(), &module) != SPV_REFLECT_RESULT_SUCCESS) {
            LOGE << "Vulkan: " << program.name << ": " << sf.file << " can't be read as SPIR-V" << std::endl;
            ok = false;
            continue;
        }
        // Descriptors: one push set. Textures keep their unit (set 1 binding N in the
        // build), uniform blocks go from set 0 binding B to 64 + B
        uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&module, &count, NULL);
        std::vector<SpvReflectDescriptorBinding*> found(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, found.data());
        for (SpvReflectDescriptorBinding* b : found) {
            Binding nb;
            if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER && b->set == 0) {
                nb.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                nb.unit = b->binding;
                nb.binding = kBlockBindingBase + b->binding;
                nb.block_size = std::max(b->block.padded_size, b->block.size);
            } else if ((b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                        b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) &&
                       b->set == 1) {
                nb.type = b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                                                                                                  : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                nb.unit = b->binding;
                nb.binding = b->binding;
                nb.view = ViewTypeOf(b->image);
                nb.multisampled = b->image.ms != 0;
                nb.shadow = b->image.depth == 1;
                nb.integer = b->type_description && (b->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT);
            } else {
                LOGE << "Vulkan: " << program.name << ": a descriptor the engine doesn't bind (" << (b->name ? b->name : "?") << ", set "
                     << b->set << ", binding " << b->binding << ", type " << (int)b->descriptor_type << ")" << std::endl;
                ok = false;
                continue;
            }
            nb.used = b->accessed != 0;
            code[b->word_offset.set] = 0;
            code[b->word_offset.binding] = nb.binding;
            auto existing = bindings.find(nb.binding);
            if (existing == bindings.end()) {
                bindings[nb.binding] = nb;
            } else {
                existing->second.block_size = std::max(existing->second.block_size, nb.block_size);
                existing->second.used = existing->second.used || nb.used;
            }
            if (nb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                // (its name and members as GL reports them)
                Block* block = NULL;
                for (Block& bl : info->blocks) {
                    if (bl.unit == nb.unit) {
                        block = &bl;
                    }
                }
                if (block == NULL) {
                    info->blocks.push_back(Block());
                    block = &info->blocks.back();
                    block->unit = nb.unit;
                    const char* type = b->type_description && b->type_description->type_name ? b->type_description->type_name : b->name;
                    block->name = type ? type : "";
                }
                block->size = std::max(block->size, nb.block_size);
                for (uint32_t i = 0; i < b->block.member_count; ++i) {
                    const SpvReflectBlockVariable& v = b->block.members[i];
                    if (v.name == NULL) {
                        continue;
                    }
                    std::string n = v.name;
                    if (v.array.dims_count > 0) {
                        n += "[0]";
                    }
                    bool have = false;
                    for (const auto& m : block->members) {
                        have = have || m.first == n;
                    }
                    if (!have) {
                        block->members.push_back(std::make_pair(n, v.offset));
                        block->members.push_back(std::make_pair(block->name + "." + n, v.offset));
                    }
                }
            }
            binding_stages[nb.binding] |= (VkShaderStageFlags)sf.stage;
            // The default block: the members SetUniform* write
            if (nb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && nb.unit == Shaders::kDefaultBlockBinding) {
                default_block_size = std::max(default_block_size, nb.block_size);
                for (uint32_t i = 0; i < b->block.member_count; ++i) {
                    const SpvReflectBlockVariable& v = b->block.members[i];
                    if (v.name == NULL) {
                        continue;
                    }
                    Member m;
                    m.offset = (GLint)v.offset;
                    m.size = v.array.dims_count > 0 ? (GLint)std::max(1u, v.array.dims[0]) : 1;
                    m.array_stride = v.array.dims_count > 0 ? (GLint)v.array.stride : 0;
                    m.matrix_stride = (GLint)v.numeric.matrix.stride;
                    m.type = GLTypeOf(v.type_description, &m.is_bool);
                    // (SPIRV-Reflect leaves it 0 on arrays of matrices: shadow_matrix[4], the
                    // light volumes': their columns all landed on the first; std140's is 16)
                    if (m.matrix_stride == 0 && v.type_description != NULL && (v.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)) {
                        m.matrix_stride = (GLint)v.type_description->traits.numeric.matrix.stride;
                        if (m.matrix_stride == 0) {
                            m.matrix_stride = 16;
                        }
                    }
                    auto seen = members.find(v.name);
                    if (seen == members.end()) {
                        members[v.name] = m;
                    } else if (seen->second.offset != m.offset) {
                        LOGE << "Vulkan: " << program.name << ": default block member " << v.name << " at two offsets" << std::endl;
                    }
                }
            }
        }
        if (sf.stage == VK_SHADER_STAGE_VERTEX_BIT) {
            spvReflectEnumerateInputVariables(&module, &count, NULL);
            std::vector<SpvReflectInterfaceVariable*> inputs(count);
            spvReflectEnumerateInputVariables(&module, &count, inputs.data());
            for (SpvReflectInterfaceVariable* v : inputs) {
                if (v->built_in != -1 || v->name == NULL) {
                    continue;
                }
                Input in;
                in.name = v->name;
                in.location = v->location;
                in.format = (VkFormat)v->format;
                info->inputs.push_back(in);
            }
        }
        if (sf.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            spvReflectEnumerateOutputVariables(&module, &count, NULL);
            std::vector<SpvReflectInterfaceVariable*> outputs(count);
            spvReflectEnumerateOutputVariables(&module, &count, outputs.data());
            for (SpvReflectInterfaceVariable* v : outputs) {
                if (v->built_in == -1) {
                    info->color_outputs = std::max(info->color_outputs, v->location + 1);
                }
            }
        }
        if (sf.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
            info->tessellation = true;
        }
        spvReflectDestroyShaderModule(&module);

        VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = code.size() * 4;
        mi.pCode = code.data();
        VkShaderModule handle = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &mi, NULL, &handle) != VK_SUCCESS) {
            LOGE << "Vulkan: " << program.name << ": vkCreateShaderModule failed for " << sf.file << std::endl;
            ok = false;
            continue;
        }
        VkPipelineShaderStageCreateInfo st = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        st.stage = sf.stage;
        st.module = handle;
        st.pName = "main";
        info->stages.push_back(st);
    }

    // The push set and the layout
    std::vector<VkDescriptorSetLayoutBinding> lb;
    for (const auto& kv : bindings) {
        VkDescriptorSetLayoutBinding b = {};
        b.binding = kv.first;
        b.descriptorType = kv.second.type;
        b.descriptorCount = 1;
        b.stageFlags = binding_stages[kv.first];
        lb.push_back(b);
        info->bindings.push_back(kv.second);
    }
    const uint32_t max_push = VkBackend::MaxPushDescriptors();
    if (lb.size() > max_push) {
        LOGE << "Vulkan: " << program.name << " reads " << lb.size() << " descriptors, more than the " << max_push << " a push set holds" << std::endl;
        ok = false;
    }
    VkDescriptorSetLayoutCreateInfo sli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    sli.bindingCount = (uint32_t)lb.size();
    sli.pBindings = lb.data();
    if (ok && vkCreateDescriptorSetLayout(device, &sli, NULL, &info->set_layout) != VK_SUCCESS) {
        ok = false;
    }
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &info->set_layout;
    if (ok && vkCreatePipelineLayout(device, &pli, NULL, &info->layout) != VK_SUCCESS) {
        ok = false;
    }

    // What GL's link gave the engine
    program.shadow_data.clear();
    program.shadow_members.clear();
    program.shadow_member_ids.clear();
    program.shadow_dirty = true;
    program.shadow_sent_frame = ~0ull;
    static const std::string debug_filter = config["vk_program_debug"].str();  // (diagnostics: a program's default block)
    if (!debug_filter.empty() && (debug_filter == "*" || program.name.find(debug_filter) != std::string::npos)) {
        for (const auto& kv : members) {
            LOGI << "Vulkan member " << program.name << " | " << kv.first << " offset " << kv.second.offset << " size " << kv.second.size
                 << " array_stride " << kv.second.array_stride << " matrix_stride " << kv.second.matrix_stride << " type " << kv.second.type
                 << std::endl;
        }
    }
    if (default_block_size > 0) {
        program.shadow_data.assign(default_block_size, 0);
        for (const auto& kv : members) {
            Program::ShadowMember m;
            m.offset = kv.second.offset;
            m.array_stride = kv.second.array_stride;
            m.matrix_stride = kv.second.matrix_stride;
            m.size = kv.second.size;
            m.type = kv.second.type;
            m.is_bool = kv.second.is_bool;
            m.refused_logged = false;
            int id = (int)program.shadow_members.size();
            program.shadow_members.push_back(m);
            program.shadow_member_ids[kv.first] = id;
        }
    }
    program.tex_uniform.assign(32, -1);
    program.sampler_units.clear();
    for (const Binding& b : info->bindings) {
        if (b.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            program.sampler_units.push_back((int)b.unit);
            if (b.unit < 32) {
                program.tex_uniform[b.unit] = (GLint)b.unit;  // (sampled: "texN" reads unit N)
            }
        }
    }
    program.attrib_address_cache.clear();
    for (const Input& in : info->inputs) {
        program.attrib_address_cache[in.name] = (GLint)in.location;
    }

    if (!ok) {
        Info* raw = info.release();
        programs[name].reset(raw);
        Destroy(name);
        return false;
    }
    programs[name] = std::move(info);
    return true;
}

std::function<void()> before_destroy;

void OnBeforeDestroy(std::function<void()> fn) {
    before_destroy = std::move(fn);
}

void Destroy(GLuint name) {
    if (shut_down) {
        return;
    }
    if (before_destroy && programs.count(name) != 0) {
        before_destroy();
    }
    auto it = programs.find(name);
    if (it == programs.end()) {
        return;
    }
    Info* info = it->second.release();
    programs.erase(it);
    VkBackend::DeferDestroy([info]() {
        VkDevice device = VkBackend::Device();
        for (const VkPipelineShaderStageCreateInfo& st : info->stages) {
            vkDestroyShaderModule(device, st.module, NULL);
        }
        if (info->layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, info->layout, NULL);
        }
        if (info->set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, info->set_layout, NULL);
        }
        delete info;
    });
}

const Info* Get(GLuint name) {
    auto it = programs.find(name);
    return it == programs.end() ? NULL : it->second.get();
}

void ForEach(const std::function<void(const Info&)>& fn) {
    for (const auto& kv : programs) {
        fn(*kv.second);
    }
}

size_t Count() {
    return programs.size();
}

int BlockIndex(GLuint program, const char* block) {
    const Info* info = Get(program);
    if (info == NULL || block == NULL) {
        return -1;
    }
    for (size_t i = 0; i < info->blocks.size(); ++i) {
        if (info->blocks[i].name == block) {
            return (int)i;
        }
    }
    return -1;
}

int BlockSize(GLuint program, int index) {
    const Info* info = Get(program);
    return info != NULL && index >= 0 && index < (int)info->blocks.size() ? (int)info->blocks[index].size : 0;
}

int MemberIndex(GLuint program, const std::string& member) {
    const Info* info = Get(program);
    if (info == NULL) {
        return -1;
    }
    for (size_t b = 0; b < info->blocks.size(); ++b) {
        const Block& bl = info->blocks[b];
        if (bl.unit == Shaders::kDefaultBlockBinding) {
            continue;  // (its members are loose uniforms: SetUniform*)
        }
        for (size_t m = 0; m < bl.members.size(); ++m) {
            if (bl.members[m].first == member) {
                return (int)(b * 4096 + m);
            }
        }
    }
    return -1;
}

int MemberOffset(GLuint program, int index) {
    const Info* info = Get(program);
    if (info == NULL || index < 0) {
        return -1;
    }
    const size_t b = (size_t)index / 4096, m = (size_t)index % 4096;
    if (b >= info->blocks.size() || m >= info->blocks[b].members.size()) {
        return -1;
    }
    return (int)info->blocks[b].members[m].second;
}

}  // namespace VkPrograms
