//-----------------------------------------------------------------------------
//           Name: shaders.h
//         Author: David Rosen
//      Developer: Wolfire Games LLC
//    Description: The shaders class holds an array of shader ID's, the
//                 shader creation states, and the current bound shader.
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

#include <Math/enginemath.h>
#include <Math/vec2.h>

#include <Graphics/textures.h>
#include <Graphics/render_backend.h>
#include <Internal/datemodified.h>
#include <Utility/flat_hash_map.hpp>

#include <opengl.h>

#define maximum_shaders 200
#define maximum_programs 200

enum ShaderType { _vertex = 0,
                  _fragment,
                  _geom,
                  _tess_ctrl,
                  _tess_eval,
                  kMaxShaderTypes };

const unsigned int UNLOADED_SHADER_ID = (unsigned int)-2;
const int MISSING_GEOM_SHADER_ID = -1;

enum BlendMode {
    _BM_NORMAL = 0,
    _BM_ADD = 1,
    _BM_MULTIPLY = 2
};

struct ShaderUniformType {
    enum Type {
        UNKNOWN = 0,

        SAMPLER2D,
        SAMPLER3D,
        SAMPLERCUBE,

        INT,
        FLOAT,

        VEC2,
        VEC3,
        VEC4,

        MAT3,
        MAT4,

        NUM_TYPES
    } type;

    operator Type() const { return type; }

    ShaderUniformType(Type t) : type(t) {}
    ShaderUniformType(const std::string& t) {
        if (t == "sampler2D") {
            type = SAMPLER2D;
        } else if (t == "sampler3D") {
            type = SAMPLER3D;
        } else if (t == "samplerCube") {
            type = SAMPLERCUBE;
        } else if (t == "int" || t == "bool") {
            type = INT;
        } else if (t == "float") {
            type = FLOAT;
        } else if (t == "vec2") {
            type = VEC2;
        } else if (t == "vec3") {
            type = VEC3;
        } else if (t == "vec4") {
            type = VEC4;
        } else if (t == "mat3") {
            type = MAT3;
        } else if (t == "mat4") {
            type = MAT4;
        } else {
            type = UNKNOWN;
        }
    }
    unsigned int size() const {
        static const unsigned int SIZES[NUM_TYPES] = {
            0,  // UNKNOWN = 0,
            0,  // SAMPLER2D,
            0,  // SAMPLER3D,
            0,  // SAMPLERCUBE,
            1,  // INT,
            1,  // FLOAT,
            2,  // VEC2,
            3,  // VEC3,
            4,  // VEC4,
            9,  // MAT3,
            16  // MAT4
        };
        return SIZES[type];
    }
};

struct ShaderUniform {
    ShaderUniformType type;
    std::string name;
    GLint address;  // uninitialized
    float* cachedValue;

    ShaderUniform() : type(ShaderUniformType::UNKNOWN), cachedValue(0) {}
    ShaderUniform(const std::string& t, const std::string& n) : type(t), name(n), cachedValue(0) {}
    void Submit();
};

class Shader {
   public:
    GLuint gl_shader;
    std::string name;
    std::string full_name;
    std::string text;
    ShaderType type;
    int64_t modified;
    bool transparent;
    bool use_tangent;
    bool bind_out_color;
    bool bind_out_vel;
    bool depth_only;
    bool particle;
    int blend_src;
    int blend_dst;
    int og_version_major;
    int og_version_minor;
    std::vector<std::string> definitions;
    std::vector<int64_t> include_modified;
    std::vector<std::string> include_files;
    std::string dump_name;  // Overgrowth Online: its file in Data/CompiledShaders (config shader_debug)
};

enum CommonShaderUniformIDs {
    NORMAL_MATRIX = 0,
    MODEL_MATRIX,

    MAX_COMMON_UNIFORMS
};

struct UniformBufferObject {
    GLuint id;
};

class Program {
   public:
    GLuint gl_program;
    std::string name;
    int shader_ids[kMaxShaderTypes];
    std::vector<GLint> tex_uniform;
    std::vector<int> sampler_units;  // Overgrowth Online: the units its "texN" samplers read
    int optional_shaders = 0;        // Overgrowth Online: the OptionalShaders it was asked for (the manifest)
    // Overgrowth Online (GL 4.6 stage 3.4): the Vulkan build's default uniform block,
    // which holds what were loose uniforms. SetUniform* write this CPU copy (members
    // found by reflection at link); a draw sends it when it changed
    struct ShadowMember {
        GLint offset, array_stride, matrix_stride, size;
        GLenum type;
        bool is_bool;
        bool refused_logged;
    };
    std::vector<unsigned char> shadow_data;  // (empty: the program has none)
    std::vector<ShadowMember> shadow_members;
    ska::flat_hash_map<std::string, int> shadow_member_ids;  // name (and "name[0]") -> member
    bool shadow_dirty = true;
    uint64_t shadow_sent_frame = ~0ull;
    GLuint shadow_sent_buffer = 0, shadow_sent_offset = 0;
    std::vector<GLuint> own_shaders;  // its stage objects when made from the Vulkan build
    typedef ska::flat_hash_map<std::string, GLint> UniformAddressCacheMap;
    UniformAddressCacheMap uniform_address_cache;
    typedef ska::flat_hash_map<GLint, GLint> UniformOffsetCacheMap;
    UniformOffsetCacheMap uniform_offset_cache;
    typedef ska::flat_hash_map<std::string, GLint> AttribAddressCacheMap;
    AttribAddressCacheMap attrib_address_cache;
    typedef ska::flat_hash_map<GLint, std::vector<GLfloat> > UniformValueCacheMap;
    UniformValueCacheMap uniform_value_cache;
    std::vector<GLint> commonShaderUniforms;

   public:
    template <typename T>
    void SetUniform(CommonShaderUniformIDs uniformID, const T& value) {
        SetUniform(commonShaderUniforms[uniformID], value);
    }
    // (Overgrowth Online stage 5.4: a default-block member goes into the program's
    // copy, Shaders::ShadowWrite; the program must be the bound one, as before)
    void SetUniform(GLint uniformID, const mat4& value);
    void SetUniform(GLint uniformID, const mat3& value);
};

class Shaders {
   private:
    std::vector<Shader> shaders;

    Shaders() : bound_program(-1), create_program_warning(false) {
        // Help fix race conditions in loading screen
        programs.reserve(1000);
        shaders.reserve(1000);
    }

   public:
    std::vector<Program> programs;
    enum ForceFlag { kNoForce,
                     kForce };  // To determine if a value should be set even if the shadow state matches
    enum OptionalShaders { kNone = 0,
                           kGeometry = 1,
                           kTesselation = 2 };

    int bound_program;
    std::string shader_dir_path;
    // If true, print a warning when a shader program is created.
    // Useful to make sure all shaders are preloaded
    Path level_path;
    bool create_program_warning;

    void setProgram(int which);
    GLint returnShaderVariable(const std::string& name, int which);
    GLint returnShaderVariableIndex(const std::string& name, int which);
    GLint returnShaderVariableOffset(int uniform_index, int program_id);
    GLint returnShaderBlockSize(int block_index, int program_id);
    int returnShader(const char* path, ShaderType type, const std::vector<std::string>& definitions);
    int returnProgram(std::string name, OptionalShaders optional_shaders = kNone);
    // Overgrowth Online (GL 4.6 stage 5.8): the program's vertex and fragment sources exist
    // (a recorded program from a mod that's gone off doesn't)
    bool ProgramSourcesExist(const std::string& name);
    // Overgrowth Online (GL 4.6 stage 3.1): compile and link failures since the last
    // clear (the shader_check bridge command); with config shader_manifest, every
    // program made is appended to <write dir>/Data/shader_manifest.txt ("flags<TAB>name")
    std::vector<std::string> compile_failures;
    // Overgrowth Online (GL 4.6 stage 3.4): IDs above this are default-block members
    // (returnShaderVariable hands them out; SetUniform* write the program's copy)
    static const GLint kShadowUniformBase = 0x40000000;
    static const GLuint kDefaultBlockBinding = 3;  // (spirv_patch.BLOCK_BINDINGS)
    void FlushShadowBlock();  // before a draw: the bound program's default block, if it has one
    void ShadowWrite(GLint var_id, const void* src, int elements, int columns, int rows, bool is_int);
    // A program's stage source as dumped to Data/CompiledShaders (empty: none)
    std::string StageDumpName(int which_program, int stage) const {
        int sid = programs[which_program].shader_ids[stage];
        return sid >= 0 ? shaders[sid].dump_name : std::string();
    }
    bool createShader(int which, ShaderType type);
    void createProgram(int which_program);
    Program* GetCurrentProgram();

    GLint GetTexUniform(int which);
    void SetUniformMat4(const std::string& var_name, const GLfloat* data);
    void SetUniformMat4(GLint var_id, const GLfloat* data);
    void SetUniformMat3(const std::string& var_name, const GLfloat* data);
    void SetUniformMat3(GLint var_id, const GLfloat* data);
    void SetUniformVec3(const std::string& var_name, const vec3& data);
    void SetUniformVec3(GLint var_id, const vec3& data);
    void SetUniformVec3(GLint var_id, const GLfloat* data);
    void SetUniformVec4(const std::string& var_name, const vec4& data);
    void SetUniformVec4(GLint var_id, const vec4& data);
    void SetUniformVec4(GLint var_id, const GLfloat* data);

    bool IsProgramTransparent(int which_program);
    int GetProgramBlendSrc(int which_program);
    int GetProgramBlendDst(int which_program);
    void noProgram();

    void Reload(bool force = false);
    void ReloadShader(int which, ShaderType type);
    void Dispose();

    static Shaders* Instance() {
        static Shaders instance;
        return &instance;
    }
    void SetUniformVec2(const std::string& var_name, const vec2& data_vec);
    void SetUniformVec2(GLint var_id, const vec2& data_vec);
    void SetUniformVec2(GLint var_id, const GLfloat* data);
    void SetUniformFloat(const std::string& var_name, const float& data);
    void SetUniformFloat(GLint var_id, const float& data);
    void SetUniformInt(const std::string& var_name, const int& data, ForceFlag force_flag = kNoForce);
    void SetUniformInt(GLint var_id, const int& data, ForceFlag force_flag = kNoForce);
    bool DoesProgramUseTangent(int which_program);
    void GetProgramOvergrowthVersion(int which_program, int& vertex_version_major, int& vertex_version_minor, int& fragment_version_major, int& fragment_version_minor);
    void SetUniformMat4Array(const std::string& var_name, const std::vector<mat4>& transforms);
    void SetUniformMat4Array(GLint var_id, const std::vector<mat4>& transforms);
    GLint returnShaderAttrib(const std::string& name, int which);
    void SetUniformVec4Array(const std::string& var_name, const std::vector<vec4>& val);
    void SetUniformVec4Array(GLint var_id, const std::vector<vec4>& val);
    void SetUniformVec3Array(const std::string& var_name, const std::vector<vec3>& val);
    void SetUniformVec3Array(GLint var_id, const std::vector<vec3>& val);
    void SetUniformVec3Array(GLint var_id, const vec3* val, int size);
    void ResetVRAM();
    int GetUBOBindIndex(int shader_id, const char* name);
};

std::string GetShaderPath(const std::string& shader_name, const std::string& shader_dir_path, ShaderType type);
