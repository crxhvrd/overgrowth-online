//-----------------------------------------------------------------------------
//           Name: imgui.vert
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Dear ImGui's draw lists through the engine (GL 4.6 stage 5.0)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#version 150

uniform mat4 ProjMtx;

in vec2 Position;
in vec2 UV;
in vec4 Color;

out vec2 Frag_UV;
out vec4 Frag_Color;

void main() {
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}
