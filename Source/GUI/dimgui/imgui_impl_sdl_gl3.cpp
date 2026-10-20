//-----------------------------------------------------------------------------
//           Name: imgui_impl_sdl_gl3.cpp
//      Developer: Wolfire Games LLC
//    Description: This is the OpenGL 3 rendering implementation from the Dear IMGUI
//                 projects with some minor modifications for use in Overgrowth.
//        License: MIT, see below
//-----------------------------------------------------------------------------
//
//  The MIT License (MIT)
//
//  Copyright(c) 2014 - 2022 Omar Cornut
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files(the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions :
//
//  The above copyright noticeand this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//-----------------------------------------------------------------------------
// ImGui SDL2 binding with OpenGL3
// In this binding, ImTextureID is used to store an OpenGL 'GLuint' texture identifier. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(), ImGui::Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include "imgui.h"
#include "imgui_impl_sdl_gl3.h"

#include <imgui-1.85/implot.h>
#include <imgui-1.85/implot_internal.h>

#include <SDL.h>
#include <SDL_syswm.h>
#include <Graphics/graphics.h>
#include <Graphics/render_backend.h>
#include <Graphics/reversez.h>
#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/shaders.h>
#include <Graphics/textures.h>
#include <Graphics/vboringcontainer.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>

// Data
static double g_Time = 0.0f;
static bool g_MousePressed[3] = {false, false, false};
static float g_MouseWheel = 0.0f;
static TextureRef g_FontTexture;

// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
//
// Overgrowth Online (GL 4.6 stage 5.0): drawn through the engine (the imgui shader in
// Data/GLSL, vertices and indices in the frame ring, state through Graphics, textures
// through Textures), so the same code draws on the Vulkan backend. A texture id is a
// Textures slot + 1 (Textures::ImGuiTextureID)
void ImGui_ImplSdlGL3_RenderDrawLists(ImDrawData* draw_data) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    Graphics* graphics = Graphics::Instance();
    Shaders* shaders = Shaders::Instance();
    Textures* textures = Textures::Instance();

    // What it changes, to restore after
    GLState last_state = graphics->CurrentGLState();
    bool last_scissor_test = graphics->ScissorTestEnabled();
    int last_program = shaders->bound_program;
    graphics->PushViewport();

    // Render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    GLState state;
    state.blend = true;
    state.blend_src = GL_SRC_ALPHA;
    state.blend_dst = GL_ONE_MINUS_SRC_ALPHA;
    state.blend_alpha_src = GL_SRC_ALPHA;
    state.blend_alpha_dst = GL_ONE_MINUS_SRC_ALPHA;
    state.cull_face = false;
    state.depth_test = false;
    state.depth_write = false;
    graphics->setGLState(state);
    graphics->setViewport(0, 0, fb_width, fb_height);
    graphics->setScissorTest(true);

    // Orthographic projection
    const float ortho_projection[4][4] =
        {
            {2.0f / io.DisplaySize.x, 0.0f, 0.0f, 0.0f},
            {0.0f, 2.0f / -io.DisplaySize.y, 0.0f, 0.0f},
            {0.0f, 0.0f, -1.0f, 0.0f},
            {-1.0f, 1.0f, 0.0f, 1.0f},
        };
    glm::mat4 proj = ReverseZ(glm::make_mat4(&ortho_projection[0][0]));
    int shader_id = shaders->returnProgram("imgui");
    shaders->setProgram(shader_id);
    shaders->SetUniformMat4("ProjMtx", (const GLfloat*)&proj);

    static VertexLayout layout;
    if (layout.Count() == 0) {
        layout.Add("Position", 0, 2, GL_FLOAT, GL_FALSE, (GLuint)offsetof(ImDrawVert, pos));
        layout.Add("UV", 0, 2, GL_FLOAT, GL_FALSE, (GLuint)offsetof(ImDrawVert, uv));
        layout.Add("Color", 0, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)offsetof(ImDrawVert, col));
    }

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        size_t vtx_bytes = (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        size_t idx_bytes = (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        FrameRing::Allocation vtx = FrameRing::Allocate(vtx_bytes, sizeof(ImDrawVert));
        memcpy(vtx.ptr, cmd_list->VtxBuffer.Data, vtx_bytes);
        FrameRing::Allocation idx = FrameRing::Allocate(idx_bytes, 4);
        memcpy(idx.ptr, cmd_list->IdxBuffer.Data, idx_bytes);

        // (per attribute: all three read the one interleaved range)
        GLuint buffers[3] = {vtx.buffer, vtx.buffer, vtx.buffer};
        GLintptr offsets[3] = {(GLintptr)vtx.offset, (GLintptr)vtx.offset, (GLintptr)vtx.offset};
        GLsizei strides[3] = {(GLsizei)sizeof(ImDrawVert), (GLsizei)sizeof(ImDrawVert), (GLsizei)sizeof(ImDrawVert)};
        graphics->BeginVertexLayout(layout, shader_id, 0x7, buffers, offsets, strides, idx.buffer);

        size_t idx_buffer_offset = 0;  // Amount of indices to skip until next drawn element
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                uintptr_t id = (uintptr_t)pcmd->TextureId;
                if (id != 0) {
                    textures->bindTexture((unsigned int)(id - 1), 0);
                }
                graphics->SetScissorRect((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
                graphics->DrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                       (void*)(uintptr_t)(idx.offset + idx_buffer_offset * sizeof(ImDrawIdx)));
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
        graphics->EndVertexLayout();
    }

    // Restore what it changed
    graphics->PopViewport();
    graphics->setGLState(last_state);
    graphics->setScissorTest(last_scissor_test);
    if (last_program >= 0) {
        shaders->setProgram(last_program);
    } else {
        shaders->noProgram();
    }
}

static const char* ImGui_ImplSdlGL3_GetClipboardText(void*) {
    return SDL_GetClipboardText();
}

static void ImGui_ImplSdlGL3_SetClipboardText(void*, const char* text) {
    SDL_SetClipboardText(text);
}

int KeyRemap(int key) {
    if (key > 127) {
        return SDLK_z + 1 + key - SDLK_CAPSLOCK;
    } else {
        return key;
    }
}

bool ImGui_ImplSdlGL3_ProcessEvent(SDL_Event* event) {
    ImGuiIO& io = ImGui::GetIO();
    switch (event->type) {
        case SDL_MOUSEWHEEL: {
            if (event->wheel.y > 0)
                g_MouseWheel = 1;
            if (event->wheel.y < 0)
                g_MouseWheel = -1;
            return true;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (event->button.button == SDL_BUTTON_LEFT) g_MousePressed[0] = true;
            if (event->button.button == SDL_BUTTON_RIGHT) g_MousePressed[1] = true;
            if (event->button.button == SDL_BUTTON_MIDDLE) g_MousePressed[2] = true;
            return true;
        }
        case SDL_TEXTINPUT: {
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int key = KeyRemap(event->key.keysym.sym);
            if (key >= 0 && key < 512) {
                io.KeysDown[key] = (event->type == SDL_KEYDOWN);
            }
            io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
            io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
            io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
            io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);

            return true;
        }
    }
    return false;
}

void ImGui_ImplSdlGL3_CreateFontsTexture() {
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);  // Load as RGBA 32-bits for OpenGL3 demo because it is more likely to be compatible with user's existing shader.

    // Upload texture to graphics system (Overgrowth Online stage 5.0: a Textures texture)
    Textures* textures = Textures::Instance();
    g_FontTexture = textures->makeTexture(width, height, GL_RGBA, GL_RGBA, false, pixels);
    textures->SetTextureName(g_FontTexture, "ImGui Font Atlas");
    textures->SetSamplingParameter(g_FontTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    textures->SetSamplingParameter(g_FontTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Store our identifier
    io.Fonts->TexID = textures->ImGuiTextureID(g_FontTexture);
}

bool ImGui_ImplSdlGL3_CreateDeviceObjects() {
    // (Overgrowth Online stage 5.0: the shader is Data/GLSL/imgui.*, the vertices go
    // through the frame ring: only the font atlas to make)
    ImGui_ImplSdlGL3_CreateFontsTexture();
    return true;
}

void ImGui_ImplSdlGL3_InvalidateDeviceObjects() {
    if (g_FontTexture.valid()) {
        g_FontTexture.clear();
        ImGui::GetIO().Fonts->TexID = 0;
    }
}

bool ImGui_ImplSdlGL3_Init(SDL_Window* window) {
    ImGuiIO& io = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Tab] = KeyRemap(SDLK_TAB);  // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    io.KeyMap[ImGuiKey_LeftArrow] = KeyRemap(SDLK_LEFT);
    io.KeyMap[ImGuiKey_RightArrow] = KeyRemap(SDLK_RIGHT);
    io.KeyMap[ImGuiKey_UpArrow] = KeyRemap(SDLK_UP);
    io.KeyMap[ImGuiKey_DownArrow] = KeyRemap(SDLK_DOWN);
    io.KeyMap[ImGuiKey_PageUp] = KeyRemap(SDLK_PAGEUP);
    io.KeyMap[ImGuiKey_PageDown] = KeyRemap(SDLK_PAGEDOWN);
    io.KeyMap[ImGuiKey_Home] = KeyRemap(SDLK_HOME);
    io.KeyMap[ImGuiKey_End] = KeyRemap(SDLK_END);
    io.KeyMap[ImGuiKey_Delete] = KeyRemap(SDLK_DELETE);
    io.KeyMap[ImGuiKey_Backspace] = KeyRemap(SDLK_BACKSPACE);
    io.KeyMap[ImGuiKey_Enter] = KeyRemap(SDLK_RETURN);
    io.KeyMap[ImGuiKey_Escape] = KeyRemap(SDLK_ESCAPE);
    io.KeyMap[ImGuiKey_A] = KeyRemap(SDLK_a);
    io.KeyMap[ImGuiKey_C] = KeyRemap(SDLK_c);
    io.KeyMap[ImGuiKey_V] = KeyRemap(SDLK_v);
    io.KeyMap[ImGuiKey_X] = KeyRemap(SDLK_x);
    io.KeyMap[ImGuiKey_Y] = KeyRemap(SDLK_y);
    io.KeyMap[ImGuiKey_Z] = KeyRemap(SDLK_z);
    io.KeyMap[ImGuiKey_LShift] = KeyRemap(SDLK_LSHIFT);
    io.KeyMap[ImGuiKey_RShift] = KeyRemap(SDLK_RSHIFT);
    io.KeyMap[ImGuiKey_NumEnter] = KeyRemap(SDLK_KP_ENTER);

    // io.RenderDrawListsFn = ImGui_ImplSdlGL3_RenderDrawLists;   // Alternatively you can set this to NULL and call ImGui::GetDrawData() after ImGui::Render() to get the same ImDrawData pointer.
    io.SetClipboardTextFn = ImGui_ImplSdlGL3_SetClipboardText;
    io.GetClipboardTextFn = ImGui_ImplSdlGL3_GetClipboardText;
    io.ClipboardUserData = NULL;

#ifdef _WIN32
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    io.ImeWindowHandle = wmInfo.info.win.window;
#else
    (void)window;
#endif

    return true;
}

void ImGui_ImplSdlGL3_Shutdown() {
    ImGui_ImplSdlGL3_InvalidateDeviceObjects();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

void ImGui_ImplSdlGL3_NewFrame(SDL_Window* window, bool ignore_mouse) {
    if (!g_FontTexture.valid())
        ImGui_ImplSdlGL3_CreateDeviceObjects();

    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    int w, h;
    int display_w, display_h;
    SDL_GetWindowSize(window, &w, &h);
    if (UseVulkan()) {  // (Overgrowth Online stage 5.2)
        VkBackend::GetDrawableSize(window, &display_w, &display_h);
    } else {
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
    }
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

    // Setup time step
    uint32_t time = SDL_GetTicks();
    double current_time = time / 1000.0;
    // If the time between frames is greater than 0, use that difference; otherwise default to 60fps
    io.DeltaTime = (current_time - g_Time) > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
    g_Time = current_time;

    if (ignore_mouse) {
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
        g_MousePressed[0] = g_MousePressed[1] = g_MousePressed[2] = false;

        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        io.MouseWheel = g_MouseWheel;
        g_MouseWheel = 0.0f;
    } else {
        // Setup inputs
        // (we already got mouse wheel, keyboard keys & characters from SDL_PollEvent())
        int mx, my;
        uint32_t mouseMask = SDL_GetMouseState(&mx, &my);
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS)
            io.MousePos = ImVec2((float)mx, (float)my);  // Mouse position, in pixels (set to -1,-1 if no mouse / on another screen, etc.)
        else
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

        io.MouseDown[0] = g_MousePressed[0] || (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
        io.MouseDown[1] = g_MousePressed[1] || (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        io.MouseDown[2] = g_MousePressed[2] || (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
        g_MousePressed[0] = g_MousePressed[1] = g_MousePressed[2] = false;

        io.MouseWheel = g_MouseWheel;
        g_MouseWheel = 0.0f;
    }

    // Hide OS mouse cursor if ImGui is drawing it
    // SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);

    // Start the frame
    ImGui::NewFrame();
}
