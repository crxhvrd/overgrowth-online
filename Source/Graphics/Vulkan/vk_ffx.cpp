//-----------------------------------------------------------------------------
//           Name: vk_ffx.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_ffx.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#include "vk_ffx.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Logging/logdata.h>

#ifdef OG_FSR

#include <Compat/fileio.h>
#include <Internal/config.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

#include <cfloat>
#include <string>

namespace VkFfx {
namespace {

struct State {
    bool tried = false;  // the DLL looked for (on the first ask)
    bool available = false;
    HMODULE dll = NULL;
    ffxFunctions fn = {};
    ffxContext context = NULL;
    uint32_t out_width = 0, out_height = 0;  // the context's (its largest render size too)
};
State s;

void FfxMessage(uint32_t type, const wchar_t* message) {
    const std::string text = message ? std::string(UTF8fromUTF16(message).c_str()) : std::string();
    if (type == FFX_API_MESSAGE_TYPE_ERROR) {
        LOGE << "FSR: " << text << std::endl;
    } else {
        LOGW << "FSR: " << text << std::endl;
    }
}

// (made again when the output's size changes: the GPU finishes with it first)
void DestroyContext() {
    if (s.context != NULL) {
        vkDeviceWaitIdle(VkBackend::Device());
        s.fn.DestroyContext(&s.context, NULL);
        s.context = NULL;
    }
}

void Shutdown() {
    DestroyContext();
    if (s.dll != NULL) {
        FreeLibrary(s.dll);
        s.dll = NULL;
    }
    s.fn = ffxFunctions();
    s.available = false;
    s.tried = false;  // (a renderer started again asks again)
}

bool Start() {
    s.tried = true;
    // Beside the game
    wchar_t exe[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring dir(exe, n);
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash != std::wstring::npos ? dir.substr(0, slash) : std::wstring(L".");
    s.dll = LoadLibraryExW((dir + L"\\amd_fidelityfx_vk.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (s.dll == NULL) {
        LOGW << "FSR: no amd_fidelityfx_vk.dll beside the game" << std::endl;
        return false;
    }
    ffxLoadFunctions(&s.fn, s.dll);
    if (s.fn.CreateContext == NULL || s.fn.DestroyContext == NULL || s.fn.Dispatch == NULL || s.fn.Query == NULL) {
        LOGW << "FSR: amd_fidelityfx_vk.dll isn't the FidelityFX API" << std::endl;
        FreeLibrary(s.dll);
        s.dll = NULL;
        return false;
    }
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkBackend::OnShutdown(&Shutdown);
    }
    s.available = true;
    LOGI << "FSR: available (amd_fidelityfx_vk.dll)" << std::endl;
    return true;
}

bool CreateContext(uint32_t out_width, uint32_t out_height) {
    DestroyContext();
    ffxCreateBackendVKDesc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = VkBackend::Device();
    backend.vkPhysicalDevice = VkBackend::PhysicalDevice();
    backend.vkDeviceProcAddr = vkGetDeviceProcAddr;
    ffxCreateContextDescUpscale desc = {};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext = &backend.header;
    // HDR in (linear, before the tone map), reversed-Z depth with the far plane at
    // infinity, exposure FSR's own; motion at the render size and without the jitter
    desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE |
                 FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    static const bool debug_checking = config["fsr_debug"].toNumber<bool>();  // (diagnostics: FSR checks what it's given)
    if (debug_checking) {
        desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    }
    desc.maxRenderSize.width = out_width;  // (every quality mode renders at most this)
    desc.maxRenderSize.height = out_height;
    desc.maxUpscaleSize.width = out_width;
    desc.maxUpscaleSize.height = out_height;
    desc.fpMessage = &FfxMessage;
    const ffxReturnCode_t r = s.fn.CreateContext(&s.context, &desc.header, NULL);
    if (r != FFX_API_RETURN_OK || s.context == NULL) {
        LOGE << "FSR: couldn't make the context for " << out_width << "x" << out_height << " (" << r << "): off" << std::endl;
        s.context = NULL;
        s.available = false;
        return false;
    }
    s.out_width = out_width;
    s.out_height = out_height;
    LOGI << "FSR: context for " << out_width << "x" << out_height << std::endl;
    return true;
}

// In GENERAL (BeginExternal, the whole image: FidelityFX's barriers cover every level), told
// so; one level described (the one it reads or writes)
FfxApiResource Wrap(const vkgl::ImageInfo& info, uint32_t usage, uint32_t state) {
    FfxApiResourceDescription d = {};
    d.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    d.format = ffxApiGetSurfaceFormatVK(info.format);
    d.width = info.width;
    d.height = info.height;
    d.depth = 1;
    d.mipCount = 1;
    d.flags = FFX_API_RESOURCE_FLAGS_NONE;
    d.usage = usage;
    return ffxApiGetResourceVK((void*)info.image, d, state);
}

}  // namespace

bool Available() {
    if (!s.tried && VkBackend::Ready()) {
        Start();
    }
    return s.available;
}

bool EvaluateFsr(const FsrFrame& f) {
    if (!Available()) {
        return false;
    }
    // (one dispatch a frame: FidelityFX frees the views it made for these 3 dispatches later,
    // which must outlast our frames in flight; the reactive mask is drawn before)
    const bool reactive = f.reactive != 0;
    const GLuint names[5] = {f.color, f.depth, f.motion, f.output, f.reactive};
    const int count = reactive ? 5 : 4;
    vkgl::ImageInfo img[5];
    int begun = 0;
    for (; begun < count; ++begun) {
        if (!vkgl::BeginExternal(names[begun], VK_IMAGE_LAYOUT_GENERAL, &img[begun], true)) {
            break;
        }
    }
    bool ok = begun == count;
    if (ok && (s.context == NULL || s.out_width != f.output_width || s.out_height != f.output_height)) {
        ok = CreateContext(f.output_width, f.output_height);
    }
    if (ok) {
        VkCommandBuffer cmd = VkBackend::TransferCommands();
        const uint32_t kIn = FFX_API_RESOURCE_STATE_COMMON;              // (GENERAL)
        const uint32_t kOut = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;  // (GENERAL)
        FfxApiResource reactive_mask = {};
        if (reactive) {
            reactive_mask = Wrap(img[4], FFX_API_RESOURCE_USAGE_READ_ONLY, kIn);
        }
        ffxDispatchDescUpscale d = {};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = cmd;
        d.color = Wrap(img[0], FFX_API_RESOURCE_USAGE_READ_ONLY, kIn);
        d.depth = Wrap(img[1], FFX_API_RESOURCE_USAGE_DEPTHTARGET, kIn);
        d.motionVectors = Wrap(img[2], FFX_API_RESOURCE_USAGE_READ_ONLY, kIn);
        d.reactive = reactive_mask;
        d.output = Wrap(img[3], FFX_API_RESOURCE_USAGE_UAV, kOut);
        d.jitterOffset.x = f.jitter_x;
        d.jitterOffset.y = f.jitter_y;
        d.motionVectorScale.x = f.motion_scale_x;
        d.motionVectorScale.y = f.motion_scale_y;
        d.renderSize.width = f.render_width;
        d.renderSize.height = f.render_height;
        d.upscaleSize.width = f.output_width;
        d.upscaleSize.height = f.output_height;
        d.enableSharpening = f.sharpness > 0.0f;
        d.sharpness = f.sharpness;
        d.frameTimeDelta = f.frame_ms;
        d.preExposure = 1.0f;
        d.reset = f.reset;
        // (reversed-Z, the far plane at infinity: near and far the other way around)
        d.cameraNear = FLT_MAX;
        d.cameraFar = f.camera_near;
        d.cameraFovAngleVertical = f.fov_vertical;
        d.viewSpaceToMetersFactor = 1.0f;
        const ffxReturnCode_t r = s.fn.Dispatch(&s.context, &d.header);
        if (r != FFX_API_RETURN_OK) {
            LOGE << "FSR: dispatch failed (" << r << "): off" << std::endl;
            DestroyContext();
            s.available = false;
            ok = false;
        }
    }
    for (int i = 0; i < begun; ++i) {
        vkgl::EndExternal(names[i], true);
    }
    return ok;
}

}  // namespace VkFfx

#else  // OG_FSR

namespace VkFfx {

bool Available() {
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGI << "FSR: not in this build (CMake's FIDELITYFX_SDK)" << std::endl;
    }
    return false;
}

bool EvaluateFsr(const FsrFrame& frame) {
    (void)frame;
    return false;
}

}  // namespace VkFfx

#endif  // OG_FSR
