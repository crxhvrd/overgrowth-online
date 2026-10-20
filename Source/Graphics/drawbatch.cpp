//-----------------------------------------------------------------------------
//           Name: drawbatch.cpp
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
#include "drawbatch.h"

#include <Graphics/textures.h>
#include <Graphics/shaders.h>
#include <Graphics/graphics.h>
#include <Graphics/camera.h>

#include <Timing/timingevent.h>
#include <Timing/intel_gl_perf.h>

#include <Internal/timer.h>
#include <Main/scenegraph.h>

extern SceneLight *primary_light;
extern Timer game_timer;

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

void DrawBatch::AddUniformMat3(std::string _name, const GLfloat *_data) {
    uniforms.resize(uniforms.size() + 1);
    uniforms.back().name = _name;
    uniforms.back().type = _mat3;
    uniforms.back().data.resize(9);
    memcpy(&uniforms.back().data[0], _data, 9 * sizeof(GLfloat));
}

void DrawBatch::AddUniformMat4(std::string _name, const GLfloat *_data) {
    uniforms.resize(uniforms.size() + 1);
    uniforms.back().name = _name;
    uniforms.back().type = _mat4;
    uniforms.back().data.resize(16);
    memcpy(&uniforms.back().data[0], _data, 16 * sizeof(GLfloat));
}

void DrawBatch::AddUniformFloat(std::string _name, GLfloat data) {
    uniforms.resize(uniforms.size() + 1);
    uniforms.back().name = _name;
    uniforms.back().type = _float;
    uniforms.back().data.resize(1);
    uniforms.back().data[0] = data;
}

void DrawBatch::AddUniformVec3(std::string _name, const GLfloat *_data) {
    uniforms.resize(uniforms.size() + 1);
    uniforms.back().name = _name;
    uniforms.back().type = _vec3;
    uniforms.back().data.resize(3);
    memcpy(&uniforms.back().data[0], _data, 3 * sizeof(GLfloat));
}

void DrawBatch::AddUniformVec3(std::string _name, const vec3 &_data) {
    AddUniformVec3(_name, (const GLfloat *)&_data);
}

DrawBatch::DrawBatch() : visible(true),
                         no_vbo(false),
                         normal_ptr(NULL),
                         vertex_ptr(NULL),
                         face_ptr(NULL),
                         num_face_ids(0),
                         draw_times(0),
                         use_cam_pos(false),
                         use_time(false),
                         use_light(false),
                         transparent(false),
                         depth_func(GL_LEQUAL),
                         polygon_offset(false),
                         shader_id(-1),
                         to_delete(false) {
    color = vec4(1.0f);
    for (int i = 0; i < 8; i++) {
        tex_coord_ptr[i] = NULL;
        tex_coord_elements[i] = 0;
        use_vbo_texcoords[i] = true;
    }
    for (auto &i : texture_ref) {
        i.clear();
    }
}

void DrawBatch::TexCoordPointer(int size, GLfloat *data, int which_tex) {
    tex_coord_ptr[which_tex] = data;
    tex_coord_elements[which_tex] = size;
}

void DrawBatch::VertexPointer(GLfloat *data) {
    vertex_ptr = data;
}

void DrawBatch::NormalPointer(GLfloat *data) {
    normal_ptr = data;
}

void DrawBatch::DrawElements(GLuint size, const GLuint *data) {
    face_ptr = data;
    num_face_ids = size;

    faces.resize(num_face_ids);
    memcpy(&faces[0], face_ptr, num_face_ids * sizeof(GLuint));

    GLuint max_vertex_id = 0;
    for (unsigned i = 0; i < num_face_ids; i++) {
        max_vertex_id = max(max_vertex_id, face_ptr[i]);
    }

    int num_vertices = max_vertex_id + 1;

    if (normal_ptr) {
        normals.resize(num_vertices * 3);
        memcpy(&normals[0], normal_ptr, normals.size() * sizeof(GLfloat));
    }

    if (vertex_ptr) {
        vertices.resize(num_vertices * 3);
        memcpy(&vertices[0], vertex_ptr, vertices.size() * sizeof(GLfloat));
    }

    for (int i = 0; i < 8; i++) {
        if (tex_coord_ptr[i]) {
            tex_coords[i].resize(num_vertices * tex_coord_elements[i]);
            memcpy(&tex_coords[i][0], tex_coord_ptr[i], tex_coords[i].size() * sizeof(GLfloat));
        }
    }

    face_ptr = NULL;
    normal_ptr = NULL;
    vertex_ptr = NULL;
    for (auto &i : tex_coord_ptr) {
        i = NULL;
    }
}

void Uniform::Apply() const {
    CHECK_GL_ERROR();
    int bound_program = Shaders::Instance()->bound_program;
    GLint var = Shaders::Instance()->returnShaderVariable(name, bound_program);

    switch (type) {
        case _mat3:
            Shaders::Instance()->SetUniformMat3(var, &data[0]);
            break;
        case _mat4:
            Shaders::Instance()->SetUniformMat4(var, &data[0]);
            break;
        case _vec3:
            Shaders::Instance()->SetUniformVec3(var, &data[0]);
            break;
        case _vec4:
            Shaders::Instance()->SetUniformVec4(var, &data[0]);
            break;
        case _float:
            Shaders::Instance()->SetUniformFloat(var, data[0]);
            break;
        default:
            // mjh - what's the reasonable thing to do here?
            break;
    }
    CHECK_GL_ERROR();
}

Uniform::Uniform() : individual(false) {
}

void DrawBatch::Dispose() {
    normals.clear();
    vertices.clear();
    for (auto &tex_coord : tex_coords) {
        tex_coord.clear();
    }
    faces.clear();
    uniforms.clear();

    vbo_faces->Dispose();
    vbo_vertices->Dispose();
    vbo_normals->Dispose();
    for (auto &vbo_tex_coord : vbo_tex_coords) {
        vbo_tex_coord->Dispose();
    }

    draw_times = 0;
}

void DrawBatch::SetStartState() const {
    Graphics *graphics = Graphics::Instance();
    Shaders *shaders = Shaders::Instance();
    Textures *textures = Textures::Instance();

    graphics->setDepthFunc(depth_func);
    graphics->setPolygonOffset(polygon_offset);
    graphics->setGLState(gl_state);

    shaders->setProgram(shader_id);

    for (const auto &uniform : uniforms) {
        uniform.Apply();
    }

    if (use_cam_pos) {
        shaders->SetUniformVec3("cam_pos", ActiveCameras::Get()->GetPos());
    }

    if (use_time) {
        shaders->SetUniformFloat("time", game_timer.GetRenderTime());
    }

    if (use_light) {
        shaders->SetUniformVec3("ws_light", primary_light->pos);
        shaders->SetUniformVec4("primary_light_color", vec4(primary_light->color, primary_light->intensity));
    }

    shaders->SetUniformVec4("uniform_color", color);

    for (int i = 0; i < kMaxTextures; i++) {
        if (texture_ref[i].valid()) {
            textures->bindTexture(texture_ref[i], i);
        }
    }

    if (transparent && graphics->use_sample_alpha_to_coverage) {
        graphics->setAlphaToCoverage(true);
    }
}

void DrawBatch::SetEndState() const {
    if (transparent && Graphics::Instance()->use_sample_alpha_to_coverage) {
        Graphics::Instance()->setAlphaToCoverage(false);
    }
}

void DrawBatch::SetUniformFloat(std::string _name, GLfloat data) {
    for (auto &uniform : uniforms) {
        if (uniform.name == _name) {
            uniform.data[0] = data;
            return;
        }
    }
    AddUniformFloat(_name, data);
}

void DrawBatch::MakeUniformIndividual(std::string _name) {
    for (auto &uniform : uniforms) {
        if (uniform.name == _name) {
            uniform.individual = true;
        }
    }
}

void DrawBatch::ApplyIndividualUniforms() const {
    for (const auto &uniform : uniforms) {
        if (uniform.individual) {
            uniform.Apply();
        }
    }
}

void DrawBatch::SetUniformVec3(std::string _name, const vec3 &_data) {
    for (auto &uniform : uniforms) {
        if (uniform.name == _name) {
            memcpy(&uniform.data[0],
                   (const GLfloat *)_data.entries,
                   3 * sizeof(GLfloat));
        }
    }
}

void DrawBatch::AddUniformVec4(std::string _name, const GLfloat *_data) {
    uniforms.resize(uniforms.size() + 1);
    uniforms.back().name = _name;
    uniforms.back().type = _vec4;
    uniforms.back().data.resize(4);
    memcpy(&uniforms.back().data[0], _data, 4 * sizeof(GLfloat));
}

void DrawBatch::AddUniformVec4(std::string _name, const vec4 &_data) {
    AddUniformVec4(_name, (const GLfloat *)&_data);
}

void DrawBatch::SetUniformVec4(std::string _name, const vec4 &_data) {
    for (auto &uniform : uniforms) {
        if (uniform.name == _name) {
            memcpy(&uniform.data[0],
                   (const GLfloat *)_data.entries,
                   4 * sizeof(GLfloat));
        }
    }
}

void DrawBatch::SetTransform(const mat4 &_transform) {
    transform = _transform;
    inv_transpose_transform = mat3(transform.GetInverseTranspose());
}
