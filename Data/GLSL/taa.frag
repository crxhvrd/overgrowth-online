//-----------------------------------------------------------------------------
//           Name: taa.frag
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Temporal anti-aliasing (Source/Graphics/temporal.h).
//                 MOTION: every pixel's motion (uv this frame minus uv last frame):
//                 what moves wrote its own, the rest is its depth reprojected into
//                 last frame's camera. RESOLVE: the jittered frame, filtered back to
//                 pixel centres, blended with last frame's result reprojected along
//                 that motion, clipped to this frame's colours around the pixel
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#version 150
#extension GL_ARB_shading_language_420pack : enable

uniform sampler2D tex0;  // RESOLVE: this frame's colour (HDR); REACTIVE: its opaque part
uniform sampler2D tex1;  // the scene's depth (reversed-Z); REACTIVE: this frame's colour
uniform sampler2D tex2;  // MOTION: what moves wrote (w: written); RESOLVE: the motion
uniform sampler2D tex3;  // RESOLVE: last frame's result

uniform int screen_width;
uniform int screen_height;
uniform mat4 reprojection;    // MOTION: this frame's clip space to last frame's
uniform vec2 jitter;          // RESOLVE: this frame's, in pixels
uniform float history_valid;  // RESOLVE: 0: no history to blend in
uniform int debug_view;       // RESOLVE (config temporal_aa_debug): 1: the motion, in pixels / 8
uniform float sharpness;      // SHARPEN: 0 none .. 1 RCAS's most

in vec2 tex;
out vec4 color;

#if defined(MOTION)

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 written = texelFetch(tex2, p, 0);
    if (written.w > 0.5) {
        color = vec4(written.xy, 0.0, 0.0);
        return;
    }
    vec2 size = vec2(screen_width, screen_height);
    vec2 uv = (vec2(p) + 0.5) / size;
    float depth = texelFetch(tex1, p, 0).r;
    // (the sky, depth 0, is a point at infinity: w 0 here, its direction turned)
    vec4 prev = reprojection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec2 motion = vec2(0.0);
    if (prev.w > 1e-6) {
        motion = uv - (prev.xy / prev.w * 0.5 + 0.5);
    }
    color = vec4(motion, 0.0, 0.0);
}

#else

// Colours are compared and blended tonemapped: a bright pixel neither wins the
// neighbourhood nor flickers (luminance weighting), then back to HDR
vec3 Tonemap(vec3 c) {
    return c / (1.0 + max(c.r, max(c.g, c.b)));
}

vec3 InvTonemap(vec3 c) {
    return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4);
}

#endif

#if defined(SHARPEN)

// The resolved frame on to the post effects, sharpened as FSR 1's RCAS does (each pixel
// against its cross, no more than its neighbours allow: no halo); the history isn't
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 last = ivec2(screen_width - 1, screen_height - 1);
    vec4 center = texelFetch(tex0, p, 0);
    vec3 e = Tonemap(max(center.rgb, vec3(0.0)));
    vec3 b = Tonemap(max(texelFetch(tex0, clamp(p + ivec2(0, 1), ivec2(0), last), 0).rgb, vec3(0.0)));
    vec3 d = Tonemap(max(texelFetch(tex0, clamp(p + ivec2(-1, 0), ivec2(0), last), 0).rgb, vec3(0.0)));
    vec3 f = Tonemap(max(texelFetch(tex0, clamp(p + ivec2(1, 0), ivec2(0), last), 0).rgb, vec3(0.0)));
    vec3 h = Tonemap(max(texelFetch(tex0, clamp(p + ivec2(0, -1), ivec2(0), last), 0).rgb, vec3(0.0)));
    vec3 mn = min(min(b, d), min(f, h));
    vec3 mx = max(max(b, d), max(f, h));
    vec3 hit_min = min(mn, e) / (4.0 * max(mx, vec3(1e-5)));
    vec3 hit_max = (vec3(1.0) - max(mx, e)) / (4.0 * min(mn, e) - 4.0);
    vec3 lobe3 = max(-hit_min, hit_max);
    float lobe = max(-0.1875, min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * sharpness;
    vec3 result = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    color = vec4(InvTonemap(clamp(result, vec3(0.0), vec3(0.999))), center.a);
}

#elif defined(REACTIVE)

// FSR's reactive mask, as its own pass makes it (FidelityFX's autogen_reactive with the
// flags we gave it: tonemapped, the largest channel's difference between the frame and its
// opaque part, under 0.2 none, else 0.9). Drawn here so FSR dispatches once a frame
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 opaque = Tonemap(max(texelFetch(tex0, p, 0).rgb, vec3(0.0)));
    vec3 frame = Tonemap(max(texelFetch(tex1, p, 0).rgb, vec3(0.0)));
    vec3 delta = abs(frame - opaque);
    float reactive = max(delta.r, max(delta.g, delta.b));
    color = vec4(reactive < 0.2 ? 0.0 : 0.9, 0.0, 0.0, 0.0);
}

#elif defined(RESOLVE)

vec3 RGBToYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

vec3 Sanitize(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) {
        return vec3(0.0);
    }
    return clamp(c, vec3(0.0), vec3(65000.0));
}

// Catmull-Rom from five bilinear taps (the history stays sharp moving)
vec3 SampleHistory(vec2 uv, vec2 size) {
    vec2 pos = uv * size;
    vec2 center = floor(pos - 0.5) + 0.5;
    vec2 f = pos - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc12 = (center + w2 / w12) / size;
    vec2 tc0 = (center - 1.0) / size;
    vec2 tc3 = (center + 2.0) / size;
    vec3 sum = textureLod(tex3, vec2(tc12.x, tc0.y), 0.0).rgb * (w12.x * w0.y) +
               textureLod(tex3, vec2(tc0.x, tc12.y), 0.0).rgb * (w0.x * w12.y) +
               textureLod(tex3, tc12, 0.0).rgb * (w12.x * w12.y) +
               textureLod(tex3, vec2(tc3.x, tc12.y), 0.0).rgb * (w3.x * w12.y) +
               textureLod(tex3, vec2(tc12.x, tc3.y), 0.0).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return Sanitize(sum / weight);
}

// The history pulled toward the box's centre until it's inside
vec3 ClipToBox(vec3 box_min, vec3 box_max, vec3 q) {
    vec3 center = 0.5 * (box_max + box_min);
    vec3 extents = 0.5 * (box_max - box_min) + vec3(1e-5);
    vec3 v = q - center;
    vec3 a = abs(v / extents);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? center + v / m : q;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 last = ivec2(screen_width - 1, screen_height - 1);
    vec2 size = vec2(screen_width, screen_height);

    // The 3x3 around the pixel: its statistics, this frame's colour back at the pixel
    // centre (each sample was drawn `jitter` away from its own), the nearest surface
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    vec3 box_min = vec3(1e9), box_max = vec3(-1e9);
    vec3 current = vec3(0.0);
    float total = 0.0;
    float nearest_depth = -1.0;
    ivec2 nearest = p;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), last);
            vec3 c = RGBToYCoCg(Tonemap(Sanitize(texelFetch(tex0, q, 0).rgb)));
            m1 += c;
            m2 += c * c;
            box_min = min(box_min, c);
            box_max = max(box_max, c);
            vec2 d = vec2(x, y) - jitter;
            float w = exp(-2.29 * dot(d, d));  // (Blackman-Harris, approximately)
            current += c * w;
            total += w;
            float depth = texelFetch(tex1, q, 0).r;  // (reversed-Z: nearer is greater)
            if (depth > nearest_depth) {
                nearest_depth = depth;
                nearest = q;
            }
        }
    }
    current /= total;
    float alpha_out = texelFetch(tex0, p, 0).a;

    vec2 uv = (vec2(p) + 0.5) / size;
    vec2 motion = texelFetch(tex2, nearest, 0).xy;  // (an edge moves with what's in front)
    if (debug_view == 1) {
        vec2 m = texelFetch(tex2, p, 0).xy * size;
        color = vec4(max(m, vec2(0.0)) / 8.0, max(-m.x, 0.0) / 8.0 + max(-m.y, 0.0) / 8.0, 1.0);
        return;
    }
    vec2 prev_uv = uv - motion;
    if (history_valid < 0.5 || any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0)))) {
        color = vec4(InvTonemap(YCoCgToRGB(current)), alpha_out);
        return;
    }

    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    const float kGamma = 1.25;
    vec3 clip_min = max(mean - kGamma * sigma, box_min);
    vec3 clip_max = min(mean + kGamma * sigma, box_max);
    vec3 history = ClipToBox(clip_min, clip_max, RGBToYCoCg(Tonemap(SampleHistory(prev_uv, size))));

    // A tenth of this frame (more while moving fast: the history is softer then)
    float speed = length(motion * size);
    float blend = mix(0.1, 0.25, clamp(speed / 16.0, 0.0, 1.0));
    vec3 result = mix(history, current, blend);
    color = vec4(InvTonemap(YCoCgToRGB(result)), alpha_out);
}

#endif
