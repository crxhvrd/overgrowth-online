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
#version 150
#extension GL_ARB_shading_language_420pack : enable

uniform samplerCube tex0;
uniform mat4 rotate;
uniform float max_angle;
uniform float src_mip;
#ifdef HEMISPHERE
	uniform vec3 hemisphere_dir;
#endif

in vec3 vec;
in vec3 face_vec;

#pragma bind_out_color
out vec4 out_color;

#define M_PI 3.1415926535897932384626433832795

// Overgrowth Online (GL 4.6 stage 5.5): an integer hash (PCG2D) of the direction,
// quantized; the fract(sin(x) * 43758.5453) one differed between APIs and vendors
float rand(vec2 co){
    uvec2 v = uvec2(ivec2(floor(co * 65536.0))) * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return float(v.x >> 8u) * (1.0 / 16777216.0);
}

void main() {    
	vec3 accum = vec3(0.0);
	vec3 front = normalize((rotate * vec4(vec,0.0)).xyz);
	vec3 right = normalize(cross(front, vec3(0.0, 1.0, 0.0)));
	vec3 up = cross(front, right);
	float rand_val = rand(face_vec.xy);
	float total = 0.0;
	int num_samples = 3;
	for(int i=-num_samples; i<num_samples; ++i){
		float spin_angle = (float(i)+rand_val) * 2.0 * M_PI / (float(num_samples*2) + 1.0);
		vec3 spin_vec = up * cos(spin_angle) + right * sin(spin_angle);
		for(int j=-num_samples; j<num_samples; ++j){
			float j_val = float(j)+rand_val;
			float angle = sign(j_val) * pow(abs(j_val/(float(num_samples)+0.5)), 0.5) * max_angle;
			vec3 sample_dir = front * cos(angle) + spin_vec * sin(angle);
			float opac = cos(angle);
			#ifdef HEMISPHERE
				opac *= step(0.0, dot(hemisphere_dir, sample_dir));
			#endif
			total += opac;
			accum += textureLod(tex0, sample_dir, src_mip).xyz * opac;
		}
	}
	out_color.xyz = accum / total;
    out_color.a = 1.0;
}
