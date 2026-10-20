//-----------------------------------------------------------------------------
//           Name: reflectioncaptureobject.cpp
//      Developer: Wolfire Games LLC
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
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
#include "reflectioncaptureobject.h"

#include <Graphics/camera.h>
#include <Graphics/shaders.h>
#include <Graphics/graphics.h>
#include <Graphics/sky.h>
#include <Graphics/models.h>

#include <Timing/timingevent.h>
#include <Timing/intel_gl_perf.h>

#include <Internal/profiler.h>
#include <Internal/common.h>

#include <Game/EntityDescription.h>
#include <Main/scenegraph.h>
#include <Editors/map_editor.h>
#include <Utility/assert.h>

extern bool g_debug_runtime_disable_reflection_capture_object_draw;

bool ReflectionCaptureObject::Initialize() {
    obj_file = "reflection_capture_object";
    sp.ASAddIntCheckbox("Global", false);
    return true;
}

void ReflectionCaptureObject::Moved(Object::MoveType type) {
    Object::Moved(type);
    dirty = true;
}

void ReflectionCaptureObject::Dispose() {
    Object::Dispose();
}

ReflectionCaptureObject::ReflectionCaptureObject() {
    box_.dims = vec3(2.0f);
}

void ReflectionCaptureObject::Draw() {
    if (g_debug_runtime_disable_reflection_capture_object_draw) {
        return;
    }

    if (scenegraph_->map_editor->IsTypeEnabled(_reflection_capture_object) &&
        cube_map_ref.valid() &&
        !Graphics::Instance()->media_mode() &&
        scenegraph_->map_editor->state_ != MapEditor::kInGame &&
        ActiveCameras::Instance()->Get()->GetFlags() == Camera::kEditorCamera) {
        PROFILER_GPU_ZONE(g_profiler_ctx, "ReflectionCaptureObject::Draw");

        Shaders* shaders = Shaders::Instance();
        Graphics* graphics = Graphics::Instance();

        GLState gl_state;
        gl_state.blend = false;
        gl_state.cull_face = true;
        gl_state.depth_test = true;
        gl_state.depth_write = true;

        graphics->setGLState(gl_state);

        int shader_id = shaders->returnProgram("reflectioncapture");
        shaders->setProgram(shader_id);

        Camera* camera = ActiveCameras::Get();
        shaders->SetUniformVec3("cam_pos", camera->GetPos());
        shaders->SetUniformMat4("model_mat", GetTransform());
        shaders->SetUniformMat4("view_mat", camera->GetViewMatrix());
        shaders->SetUniformMat4("proj_mat", camera->GetProjMatrix());

        Textures::Instance()->bindTexture(cube_map_ref);

        Model* probe_model = &Models::Instance()->GetModel(scenegraph_->light_probe_collection.probe_model_id);
        if (!probe_model->vbo_loaded) {
            probe_model->createVBO();
        }
        // Overgrowth Online (GL 4.6 stage 2.4): the probe sphere (positions only)
        static VertexLayout layout;
        if (layout.Count() == 0) {
            layout.Add("vertex", 0, 3);
        }
        VBOContainer* vbos[1] = {&probe_model->VBO_vertices};
        graphics->BeginVertexLayout(layout, shader_id, vbos, probe_model->VBO_faces.id());
        graphics->DrawElements(GL_TRIANGLES, (unsigned int)probe_model->faces.size(), GL_UNSIGNED_INT, 0);
        graphics->EndVertexLayout();
        graphics->ResetVertexAttribArrays();
    }
}

ReflectionCaptureObject::~ReflectionCaptureObject() {
}

void ReflectionCaptureObject::GetDisplayName(char* buf, int buf_size) {
    if (GetName().empty()) {
        FormatString(buf, buf_size, "%d: Reflection Capture", GetID());
    } else {
        FormatString(buf, buf_size, "%s: Reflection Capture", GetName().c_str());
    }
}

void ReflectionCaptureObject::GetDesc(EntityDescription& desc) const {
    Object::GetDesc(desc);

    const size_t data_size = kLightProbeNumCoeffs * sizeof(float);
    std::vector<char> data(data_size);

    memcpy(&data[0], &avg_color, data_size);
    desc.AddData(EDF_GI_COEFFICIENTS, data);
}

bool ReflectionCaptureObject::SetFromDesc(const EntityDescription& desc) {
    bool ret = Object::SetFromDesc(desc);
    if (ret) {
        for (const auto& field : desc.fields) {
            switch (field.type) {
                case EDF_GI_COEFFICIENTS: {
                    memcpy(avg_color, &field.data[0], kLightProbeNumCoeffs * sizeof(float));
                    break;
                }
            }
        }
    }
    return ret;
}
