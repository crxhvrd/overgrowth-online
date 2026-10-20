//-----------------------------------------------------------------------------
//           Name: editor_tools.cpp
//      Developer: Wolfire Games LLC
//         Author: Phillip Isola
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
#include "editor_tools.h"

#include <Graphics/graphics.h>
#include <Graphics/shaders.h>

#include <Timing/timingevent.h>
#include <Timing/intel_gl_perf.h>

#include <Internal/profiler.h>

BoxSelector::BoxSelector() : acting(false), shader_id_(-1) {
    gl_state_.blend = true;
    gl_state_.cull_face = false;
    gl_state_.depth_test = false;
    gl_state_.depth_write = false;
}

void BoxSelector::Draw() {
    PROFILER_GPU_ZONE(g_profiler_ctx, "DrawBillboard()");
    Graphics* graphics = Graphics::Instance();
    Shaders* shaders = Shaders::Instance();
    // Choose appearance parameters
    const vec4 fill_color(0.1f, 1.0f, 0.4f, 0.25f);
    const vec4 edge_color(0.0f, 0.0f, 0.0f, 1.0f);
    const float kEdgeRadius = 1.0f;
    // Set up projection matrix
    mat4 proj_matrix;
    proj_matrix.SetOrtho(0.0f, (float)graphics->window_dims[0], 0.0f, (float)graphics->window_dims[1], -100.0f, 100.0f);
    // Set up vbo for green fill area
    GLfloat fill_verts[8] = {points[0][0], points[0][1], points[1][0], points[0][1], points[1][0], points[1][1], points[0][0], points[1][1]};
    fill_vbo_.Fill(kVBOFloat | kVBODynamic, sizeof(fill_verts), fill_verts);
    if (!fill_index_vbo_.valid()) {
        static const GLuint index[] = {0, 1, 2, 0, 2, 3};
        fill_index_vbo_.Fill(kVBOElement | kVBOStatic, sizeof(index), (void*)index);
    }
    // Set up vbo for black edge
    const float e = (points[0][0] < points[1][0] && points[0][1] < points[1][1]) ? kEdgeRadius : -kEdgeRadius;  // edge width
    GLfloat edge_verts[32] = {
        points[0][0] - e, points[0][1] - e, points[1][0] + e, points[0][1] - e, points[1][0] + e, points[0][1] + e, points[0][0] - e, points[0][1] + e,
        points[1][0] - e, points[0][1] - e, points[1][0] + e, points[0][1] - e, points[1][0] + e, points[1][1] + e, points[1][0] - e, points[1][1] + e,
        points[0][0] - e, points[1][1] - e, points[1][0] + e, points[1][1] - e, points[1][0] + e, points[1][1] + e, points[0][0] - e, points[1][1] + e,
        points[0][0] - e, points[0][1] - e, points[0][0] + e, points[0][1] - e, points[0][0] + e, points[1][1] + e, points[0][0] - e, points[1][1] + e};
    edge_vbo_.Fill(kVBOFloat | kVBODynamic, sizeof(edge_verts), edge_verts);
    if (!edge_index_vbo_.valid()) {
        static const GLuint index[] = {0, 1, 2, 0, 2, 3,
                                       4, 5, 6, 4, 6, 7,
                                       8, 9, 10, 8, 10, 11,
                                       12, 13, 14, 12, 14, 15};
        edge_index_vbo_.Fill(kVBOElement | kVBOStatic, sizeof(index), (void*)index);
    }
    // Set GL and shader state
    graphics->setGLState(gl_state_);
    if (shader_id_ == -1) {
        shader_id_ = shaders->returnProgram("simple_2d #FLIPPED");
    }
    shaders->setProgram(shader_id_);
    shaders->SetUniformMat4("mvp_mat", proj_matrix.entries);
    // Overgrowth Online (GL 4.6 stage 2.4): 2D corners
    static VertexLayout layout;
    if (layout.Count() == 0) {
        layout.Add("vert_coord", 0, 2);
    }
    // Draw fill
    shaders->SetUniformVec4("color", fill_color);
    VBOContainer* fill_vbos[1] = {&fill_vbo_};
    graphics->BeginVertexLayout(layout, shader_id_, fill_vbos, fill_index_vbo_.id());
    graphics->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    // Draw edge
    shaders->SetUniformVec4("color", edge_color);
    VBOContainer* edge_vbos[1] = {&edge_vbo_};
    graphics->BeginVertexLayout(layout, shader_id_, edge_vbos, edge_index_vbo_.id());
    graphics->DrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_INT, 0);
    graphics->EndVertexLayout();
    graphics->ResetVertexAttribArrays();
}
