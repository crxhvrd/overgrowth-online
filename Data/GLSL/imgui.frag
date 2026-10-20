//-----------------------------------------------------------------------------
//           Name: imgui.frag
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Dear ImGui's draw lists through the engine (GL 4.6 stage 5.0)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#version 150

uniform sampler2D tex0;

in vec2 Frag_UV;
in vec4 Frag_Color;

out vec4 out_color;

void main() {
    out_color = Frag_Color * texture(tex0, Frag_UV.st);
}
