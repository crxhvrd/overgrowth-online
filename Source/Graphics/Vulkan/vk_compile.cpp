//-----------------------------------------------------------------------------
//           Name: vk_compile.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_compile.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "vk_compile.h"

#include <Internal/filesystem.h>
#include <Internal/modid.h>
#include <Compat/fileio.h>
#include <Logging/logdata.h>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace VkCompile {
namespace {

const char* const kVersion = "1";  // (the cache key: bump when what's made from a source changes)
const char* const kFiles[kStages] = {"vert.spv", "tesc.spv", "tese.spv", "geom.spv", "frag.spv"};
const char* const kStageNames[kStages] = {"vertex", "tessellation control", "tessellation evaluation", "geometry", "fragment"};
const glslang_stage_t kGlslangStages[kStages] = {GLSLANG_STAGE_VERTEX, GLSLANG_STAGE_TESSCONTROL, GLSLANG_STAGE_TESSEVALUATION,
                                                 GLSLANG_STAGE_GEOMETRY, GLSLANG_STAGE_FRAGMENT};

// ---- glslang, loaded the first time a program needs it

struct Glslang {
    bool tried = false, ok = false;
    std::string version, from;
    const glslang_resource_t* resource = NULL;
    decltype(&glslang_initialize_process) initialize_process = NULL;
    decltype(&glslang_get_version) get_version = NULL;
    decltype(&glslang_shader_create) shader_create = NULL;
    decltype(&glslang_shader_delete) shader_delete = NULL;
    decltype(&glslang_shader_set_options) shader_set_options = NULL;
    decltype(&glslang_shader_preprocess) shader_preprocess = NULL;
    decltype(&glslang_shader_parse) shader_parse = NULL;
    decltype(&glslang_shader_get_info_log) shader_get_info_log = NULL;
    decltype(&glslang_program_create) program_create = NULL;
    decltype(&glslang_program_delete) program_delete = NULL;
    decltype(&glslang_program_add_shader) program_add_shader = NULL;
    decltype(&glslang_program_link) program_link = NULL;
    decltype(&glslang_program_map_io) program_map_io = NULL;
    decltype(&glslang_program_get_info_log) program_get_info_log = NULL;
    decltype(&glslang_program_SPIRV_generate) program_spirv_generate = NULL;
    decltype(&glslang_program_SPIRV_get_size) program_spirv_get_size = NULL;
    decltype(&glslang_program_SPIRV_get) program_spirv_get = NULL;
    decltype(&glslang_program_SPIRV_get_messages) program_spirv_get_messages = NULL;
};
Glslang g;

#ifdef _WIN32
typedef HMODULE Library;

// The DLL in `dir` (its own dependencies looked for beside it)
Library Open(const std::wstring& dir, const wchar_t* file) {
    std::wstring path = dir + L"\\" + file;
    return LoadLibraryExW(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void* Symbol(Library lib, const char* name) {
    return (void*)GetProcAddress(lib, name);
}
#else
typedef void* Library;

Library Open(const std::string& dir, const char* file) {
    return dlopen((dir.empty() ? std::string(file) : dir + "/" + file).c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* Symbol(Library lib, const char* name) {
    return dlsym(lib, name);
}
#endif

template <typename T>
bool Bind(Library lib, const char* name, T* fn) {
    *fn = (T)Symbol(lib, name);
    if (!*fn) {
        LOGE << "Vulkan: " << name << " is missing from glslang" << std::endl;
    }
    return *fn != NULL;
}

bool Load() {
    if (g.tried) {
        return g.ok;
    }
    g.tried = true;
    Library lib = NULL, limits = NULL;
#ifdef _WIN32
    // Beside the game first, else the Vulkan SDK's (a developer's PC)
    std::vector<std::wstring> dirs;
    wchar_t exe[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring dir(exe, n);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dirs.push_back(dir.substr(0, slash));
        }
    }
    wchar_t sdk[MAX_PATH] = {0};
    n = GetEnvironmentVariableW(L"VULKAN_SDK", sdk, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        dirs.push_back(std::wstring(sdk, n) + L"\\Bin");
    }
    for (const std::wstring& dir : dirs) {
        lib = Open(dir, L"glslang.dll");
        limits = lib ? Open(dir, L"glslang-default-resource-limits.dll") : NULL;
        if (lib && limits) {
            g.from = UTF8fromUTF16(dir).c_str();  // (cut at its terminator)
            break;
        }
        if (lib) {
            FreeLibrary(lib);
            lib = NULL;
        }
    }
#else
    lib = Open(std::string(), "libglslang.so");
    limits = lib ? Open(std::string(), "libglslang-default-resource-limits.so") : NULL;
    g.from = "the library path";
#endif
    if (!lib || !limits) {
        LOGE << "Vulkan: no glslang (glslang.dll and glslang-default-resource-limits.dll beside the game): programs missing from "
                "the SPIR-V build can't be made"
             << std::endl;
        return false;
    }
    decltype(&glslang_default_resource) default_resource = NULL;
    bool ok = Bind(limits, "glslang_default_resource", &default_resource);
    ok = Bind(lib, "glslang_initialize_process", &g.initialize_process) && ok;
    ok = Bind(lib, "glslang_get_version", &g.get_version) && ok;
    ok = Bind(lib, "glslang_shader_create", &g.shader_create) && ok;
    ok = Bind(lib, "glslang_shader_delete", &g.shader_delete) && ok;
    ok = Bind(lib, "glslang_shader_set_options", &g.shader_set_options) && ok;
    ok = Bind(lib, "glslang_shader_preprocess", &g.shader_preprocess) && ok;
    ok = Bind(lib, "glslang_shader_parse", &g.shader_parse) && ok;
    ok = Bind(lib, "glslang_shader_get_info_log", &g.shader_get_info_log) && ok;
    ok = Bind(lib, "glslang_program_create", &g.program_create) && ok;
    ok = Bind(lib, "glslang_program_delete", &g.program_delete) && ok;
    ok = Bind(lib, "glslang_program_add_shader", &g.program_add_shader) && ok;
    ok = Bind(lib, "glslang_program_link", &g.program_link) && ok;
    ok = Bind(lib, "glslang_program_map_io", &g.program_map_io) && ok;
    ok = Bind(lib, "glslang_program_get_info_log", &g.program_get_info_log) && ok;
    ok = Bind(lib, "glslang_program_SPIRV_generate", &g.program_spirv_generate) && ok;
    ok = Bind(lib, "glslang_program_SPIRV_get_size", &g.program_spirv_get_size) && ok;
    ok = Bind(lib, "glslang_program_SPIRV_get", &g.program_spirv_get) && ok;
    ok = Bind(lib, "glslang_program_SPIRV_get_messages", &g.program_spirv_get_messages) && ok;
    if (!ok || !g.initialize_process()) {
        return false;
    }
    g.resource = default_resource();
    glslang_version_t v = {};
    g.get_version(&v);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d.%d.%d%s%s", v.major, v.minor, v.patch, v.flavor && *v.flavor ? "-" : "", v.flavor ? v.flavor : "");
    g.version = buf;
    LOGI << "Vulkan: programs missing from the SPIR-V build are compiled with glslang " << g.version << " (" << g.from << ")" << std::endl;
    g.ok = g.resource != NULL;
    return g.ok;
}

// ---- the build's patches (Tools/ogmp/spirv_patch.py)

const uint32_t kOpName = 5, kOpDecorate = 71, kOpVariable = 59, kOpConstant = 43;
const uint32_t kOpTypeBool = 20, kOpTypeInt = 21, kOpTypeFloat = 22, kOpTypeVector = 23, kOpTypeMatrix = 24;
const uint32_t kOpTypeArray = 28, kOpTypeStruct = 30, kOpTypePointer = 32;
const uint32_t kDecLocation = 30, kDecBinding = 33, kDecSet = 34;
const uint32_t kUniformConstant = 0, kInput = 1, kUniform = 2, kOutput = 3;

struct Module {
    std::vector<uint32_t>& words;
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, std::map<uint32_t, size_t> > decorations;  // id -> decoration -> index of its first literal
    std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t> > > vars;  // id -> (pointer type, storage class), in order
    std::map<uint32_t, uint32_t> consts;
    std::map<uint32_t, std::pair<uint32_t, std::vector<uint32_t> > > types;  // id -> (opcode, operands after the id)

    explicit Module(std::vector<uint32_t>& w) : words(w) {
        size_t i = 5;
        while (i < words.size()) {
            const uint32_t op = words[i] & 0xFFFF, count = words[i] >> 16;
            if (count == 0 || i + count > words.size()) {
                break;
            }
            const uint32_t* a = &words[i + 1];
            const uint32_t n = count - 1;
            if (op == kOpName && n >= 2) {
                const char* s = (const char*)(a + 1);
                names[a[0]] = std::string(s, strnlen(s, (n - 1) * 4));
            } else if (op == kOpDecorate && n >= 2) {
                decorations[a[0]][a[1]] = i + 3;
            } else if (op == kOpVariable && n >= 3) {
                vars.push_back(std::make_pair(a[1], std::make_pair(a[0], a[2])));
            } else if (op == kOpConstant && n >= 2) {
                consts[a[1]] = n > 2 ? a[2] : 0;
            } else if ((op == kOpTypeInt || op == kOpTypeFloat || op == kOpTypeBool || op == kOpTypeVector || op == kOpTypeMatrix ||
                        op == kOpTypeArray || op == kOpTypeStruct || op == kOpTypePointer) &&
                       n >= 1) {
                types[a[0]] = std::make_pair(op, std::vector<uint32_t>(a + 1, a + n));
            }
            i += count;
        }
    }

    bool Has(uint32_t id, uint32_t decoration) const {
        auto it = decorations.find(id);
        return it != decorations.end() && it->second.count(decoration) != 0;
    }

    void Set(uint32_t id, uint32_t decoration, uint32_t value) {
        words[decorations[id][decoration]] = value;
    }

    std::string Name(uint32_t id) const {
        auto it = names.find(id);
        return it == names.end() ? "%" + std::to_string(id) : it->second;
    }

    uint32_t Pointee(uint32_t pointer_type) const {
        auto it = types.find(pointer_type);
        return it != types.end() && it->second.second.size() >= 2 ? it->second.second[1] : 0;
    }

    // An Input or Output variable's name -> (id, type) (those with a Location: not built-ins)
    std::map<std::string, std::pair<uint32_t, uint32_t> > Interface(uint32_t storage) const {
        std::map<std::string, std::pair<uint32_t, uint32_t> > out;
        for (const auto& v : vars) {
            if (v.second.second == storage && Has(v.first, kDecLocation)) {
                out[Name(v.first)] = std::make_pair(v.first, Pointee(v.second.first));
            }
        }
        return out;
    }

    uint32_t Locations(uint32_t type, bool strip_outer_array = false) const {
        auto it = types.find(type);
        if (it == types.end()) {
            return 1;
        }
        const uint32_t op = it->second.first;
        const std::vector<uint32_t>& a = it->second.second;
        if (op == kOpTypeArray && a.size() >= 2) {
            if (strip_outer_array) {
                return Locations(a[0]);
            }
            auto c = consts.find(a[1]);
            return (c == consts.end() ? 1 : c->second) * Locations(a[0]);
        }
        if (op == kOpTypeMatrix && a.size() >= 2) {
            return a[1];
        }
        if (op == kOpTypeStruct) {
            uint32_t sum = 0;
            for (uint32_t m : a) {
                sum += Locations(m);
            }
            return sum;
        }
        return 1;  // (scalars and vectors; no doubles in the engine's shaders)
    }
};

// Every inter-stage variable gets one location by name (names sorted, packed), the same in
// each stage it's in; vertex inputs and fragment outputs keep theirs
void UnifyLocations(std::vector<std::pair<int, Module*> >& modules) {
    std::map<std::string, uint32_t> sizes;
    for (auto& sm : modules) {
        const int stage = sm.first;
        Module& m = *sm.second;
        if (stage != kVert) {
            for (const auto& v : m.Interface(kInput)) {
                const bool per_vertex = stage == kGeom || stage == kTesc || stage == kTese;
                sizes[v.first] = std::max(sizes[v.first], m.Locations(v.second.second, per_vertex));
            }
        }
        if (stage != kFrag) {
            for (const auto& v : m.Interface(kOutput)) {
                sizes[v.first] = std::max(sizes[v.first], m.Locations(v.second.second, stage == kTesc));
            }
        }
    }
    std::map<std::string, uint32_t> assigned;
    uint32_t location = 0;
    for (const auto& s : sizes) {
        assigned[s.first] = location;
        location += s.second;
    }
    for (auto& sm : modules) {
        Module& m = *sm.second;
        if (sm.first != kVert) {
            for (const auto& v : m.Interface(kInput)) {
                m.Set(v.second.first, kDecLocation, assigned[v.first]);
            }
        }
        if (sm.first != kFrag) {
            for (const auto& v : m.Interface(kOutput)) {
                m.Set(v.second.first, kDecLocation, assigned[v.first]);
            }
        }
    }
}

// The engine's bindings (stage 3.3): uniform blocks in set 0 at the binding points the
// engine binds them to, textures in set 1 at their unit ("texN" -> N). False: a
// resource the engine doesn't bind (its name in *unknown)
bool AssignBindings(Module& m, std::string* unknown) {
    static const std::map<std::string, uint32_t> kBlocks = {
        {"InstanceInfo", 0}, {"ClusterInfo", 1}, {"ShadowCascades", 2}, {"gl_DefaultUniformBlock", 3}, {"LightProbeInfo", 4}};
    bool ok = true;
    for (const auto& v : m.vars) {
        const uint32_t id = v.first, pointer = v.second.first, storage = v.second.second;
        if (!m.Has(id, kDecBinding)) {
            continue;
        }
        if (storage == kUniformConstant) {
            const std::string name = m.names.count(id) ? m.names[id] : std::string();
            int unit = -1;
            if (name == "tex") {
                unit = 0;
            } else if (name.size() > 3 && name.compare(0, 3, "tex") == 0 && name.find_first_not_of("0123456789", 3) == std::string::npos) {
                unit = atoi(name.c_str() + 3);
            }
            if (unit < 0) {
                *unknown += (unknown->empty() ? "" : ", ") + name;
                ok = false;
                continue;
            }
            m.Set(id, kDecBinding, (uint32_t)unit);
            if (m.Has(id, kDecSet)) {
                m.Set(id, kDecSet, 1);
            }
        } else {
            const uint32_t block = m.Pointee(pointer);
            const std::string name = m.names.count(block) ? m.names[block] : std::string();
            auto it = kBlocks.find(name);
            if (storage != kUniform || it == kBlocks.end()) {
                *unknown += (unknown->empty() ? "" : ", ") + name;
                ok = false;
                continue;
            }
            m.Set(id, kDecBinding, it->second);
            if (m.Has(id, kDecSet)) {
                m.Set(id, kDecSet, 0);
            }
        }
    }
    return ok;
}

// ---- compiling

bool Compile(const char* const sources[kStages], std::vector<uint32_t> out[kStages], std::string* error) {
    const glslang_messages_t messages = (glslang_messages_t)(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    glslang_shader_t* shaders[kStages] = {};
    glslang_program_t* program = g.program_create();
    bool ok = true;
    // (spirv_build.py: glslangValidator -R -V --target-env vulkan1.2 --amb --aml -l)
    for (int s = 0; s < kStages && ok; ++s) {
        if (!sources[s]) {
            continue;
        }
        glslang_input_t input = {};
        input.language = GLSLANG_SOURCE_GLSL;
        input.stage = kGlslangStages[s];
        input.client = GLSLANG_CLIENT_VULKAN;
        input.client_version = GLSLANG_TARGET_VULKAN_1_2;
        input.target_language = GLSLANG_TARGET_SPV;
        input.target_language_version = GLSLANG_TARGET_SPV_1_5;
        input.code = sources[s];
        input.default_version = 100;
        input.default_profile = GLSLANG_NO_PROFILE;
        input.messages = messages;
        input.resource = g.resource;
        shaders[s] = g.shader_create(&input);
        g.shader_set_options(shaders[s], GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS | GLSLANG_SHADER_VULKAN_RULES_RELAXED);
        if (!g.shader_preprocess(shaders[s], &input) || !g.shader_parse(shaders[s], &input)) {
            *error = std::string(kStageNames[s]) + " stage: " + g.shader_get_info_log(shaders[s]);
            ok = false;
            break;
        }
        g.program_add_shader(program, shaders[s]);
    }
    if (ok && (!g.program_link(program, messages) || !g.program_map_io(program))) {
        *error = std::string("link: ") + g.program_get_info_log(program);
        ok = false;
    }
    for (int s = 0; s < kStages && ok; ++s) {
        if (!sources[s]) {
            continue;
        }
        g.program_spirv_generate(program, kGlslangStages[s]);
        out[s].resize(g.program_spirv_get_size(program));
        if (out[s].empty()) {
            const char* m = g.program_spirv_get_messages(program);
            *error = std::string(kStageNames[s]) + " stage: no SPIR-V" + (m && *m ? std::string(": ") + m : std::string());
            ok = false;
            break;
        }
        g.program_spirv_get(program, out[s].data());
    }
    g.program_delete(program);
    for (glslang_shader_t* s : shaders) {
        if (s) {
            g.shader_delete(s);
        }
    }
    if (!ok) {
        return false;
    }
    // The build's patches
    std::vector<Module> modules;
    modules.reserve(kStages);
    std::vector<std::pair<int, Module*> > order;
    for (int s = 0; s < kStages; ++s) {
        if (sources[s]) {
            modules.emplace_back(out[s]);
            order.push_back(std::make_pair(s, &modules.back()));
        }
    }
    UnifyLocations(order);
    std::string unknown;
    for (auto& sm : order) {
        AssignBindings(*sm.second, &unknown);
    }
    if (!unknown.empty()) {
        *error = "resources the engine doesn't bind: " + unknown;
        return false;
    }
    // Fragment outputs where the engine's render targets want them
    if (sources[kFrag]) {
        for (const auto& v : modules.back().Interface(kOutput)) {
            const auto& d = modules.back().decorations[v.second.first];
            const uint32_t location = modules.back().words[d.at(kDecLocation)];
            if ((v.first == "out_color" && location != 0) || (v.first == "out_vel" && location != 1)) {
                *error = v.first + " at location " + std::to_string(location);
                return false;
            }
        }
    }
    return true;
}

uint64_t Fnv(const void* data, size_t n, uint64_t h) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

bool Exists(const std::string& path) {
    FILE* f = my_fopen(path.c_str(), "rb");
    if (f) {
        fclose(f);
    }
    return f != NULL;
}

bool Write(const std::string& path, const void* data, size_t bytes) {
    FILE* f = my_fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(data, 1, bytes, f) == bytes;
    return fclose(f) == 0 && ok;
}

}  // namespace

std::string ProgramDir(const std::string& name, const char* const sources[kStages]) {
    if (!sources[kVert] || !sources[kFrag] || !Load()) {
        return std::string();
    }
    uint64_t h = 1469598103934665603ull;
    h = Fnv(kVersion, strlen(kVersion) + 1, h);
    h = Fnv(g.version.c_str(), g.version.size() + 1, h);
    for (int s = 0; s < kStages; ++s) {
        const char* text = sources[s] ? sources[s] : "";
        h = Fnv(&s, sizeof(s), h);
        h = Fnv(text, strlen(text) + 1, h);
    }
    char key[32];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)h);
    const std::string dir = GetWritePath(CoreGameModID) + "Data/VulkanShaders/" + key;
    const std::string done = dir + "/program.txt";  // (written last: the directory is complete)
    if (Exists(done)) {
        return dir;
    }
    const uint32_t start = SDL_GetTicks();
    std::vector<uint32_t> spirv[kStages];
    std::string error;
    if (!Compile(sources, spirv, &error)) {
        LOGE << "Vulkan: " << name << " doesn't compile: " << error.substr(0, 2000) << std::endl;
        return std::string();
    }
    CreateParentDirs(done);
    for (int s = 0; s < kStages; ++s) {
        if (!spirv[s].empty() && !Write(dir + "/" + kFiles[s], spirv[s].data(), spirv[s].size() * 4)) {
            LOGE << "Vulkan: can't write " << dir << "/" << kFiles[s] << std::endl;
            return std::string();
        }
    }
    const std::string note = name + "\n";
    Write(done, note.c_str(), note.size());
    LOGI << "Vulkan: compiled " << name << " (" << (SDL_GetTicks() - start) << " ms)" << std::endl;
    return dir;
}

}  // namespace VkCompile
