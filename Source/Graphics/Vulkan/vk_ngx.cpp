//-----------------------------------------------------------------------------
//           Name: vk_ngx.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_ngx.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#include "vk_ngx.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/Vulkan/vk_gl.h>
#include <Logging/logdata.h>

#ifdef OG_DLSS

#include <Graphics/graphics.h>
#include <Internal/config.h>
#include <Internal/filesystem.h>
#include <Internal/modid.h>
#include <Compat/fileio.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstring>
#include <string>

namespace VkNgx {
namespace {

// NGX wants an NVIDIA application id or a project id (a GUID of our own)
const char* const kProjectId = "6d1f3a92-5c4e-4b8a-9e07-2f6b8c1d4a53";
const char* const kEngineVersion = "0.40";

struct State {
    bool tried = false;       // NGX started (or failed to) on the first ask
    bool available = false;
    bool started = false;     // NVSDK_NGX_VULKAN_Init succeeded (Shutdown1 owed)
    NVSDK_NGX_Parameter* params = NULL;
    NVSDK_NGX_Handle* feature = NULL;
    uint32_t width = 0, height = 0, out_width = 0, out_height = 0;
    int quality = -1, preset = -1;
    std::wstring data_path;  // NGX's logs (kept alive: NGX holds the pointer)
    // Extension names handed to the instance and device creation outlive the calls
    std::vector<std::string> instance_names, device_names;
};
State s;

NVSDK_NGX_FeatureCommonInfo common_info = {};

void NGXLog(const char* message, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature component) {
    (void)level;
    (void)component;
    std::string line = message ? message : "";
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    LOGI << "NGX: " << line << std::endl;
}

const NVSDK_NGX_FeatureCommonInfo* CommonInfo() {
    static const bool log = config["dlss_log"].toNumber<bool>();  // (diagnostics: NGX's log in ours)
    if (log) {
        common_info.LoggingInfo.LoggingCallback = &NGXLog;
        common_info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    }
    return &common_info;
}

NVSDK_NGX_FeatureDiscoveryInfo Discovery() {
    NVSDK_NGX_FeatureDiscoveryInfo info;
    memset(&info, 0, sizeof(info));
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    info.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    info.Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    info.ApplicationDataPath = L".";
    info.FeatureInfo = CommonInfo();
    return info;
}

bool Has(const std::vector<const char*>& list, const char* name) {
    for (const char* e : list) {
        if (strcmp(e, name) == 0) {
            return true;
        }
    }
    return false;
}

// (made again on a resize or a mode change: the GPU finishes with it first)
void ReleaseFeature() {
    if (s.feature != NULL) {
        vkDeviceWaitIdle(VkBackend::Device());
        NVSDK_NGX_VULKAN_ReleaseFeature(s.feature);
        s.feature = NULL;
    }
}

void Shutdown() {
    if (s.feature != NULL) {
        NVSDK_NGX_VULKAN_ReleaseFeature(s.feature);  // (the device is idle here)
        s.feature = NULL;
    }
    if (s.params != NULL) {
        NVSDK_NGX_VULKAN_DestroyParameters(s.params);
        s.params = NULL;
    }
    if (s.started) {
        NVSDK_NGX_VULKAN_Shutdown1(VkBackend::Device());
        s.started = false;
    }
    s.available = false;
    s.tried = false;  // (a renderer started again asks again)
}

bool Start() {
    s.tried = true;
    if (VkBackend::VendorID() != 0x10DE) {
        LOGI << "DLSS: not an NVIDIA GPU" << std::endl;
        return false;
    }
    s.data_path = UTF16fromUTF8(GetWritePath(CoreGameModID));
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, s.data_path.c_str(),
                                                              VkBackend::Instance(), VkBackend::PhysicalDevice(), VkBackend::Device(),
                                                              vkGetInstanceProcAddr, vkGetDeviceProcAddr, CommonInfo(), NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r)) {
        LOGW << "DLSS: NGX didn't start (0x" << std::hex << (unsigned)r << std::dec << ")" << std::endl;
        return false;
    }
    s.started = true;
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        VkBackend::OnShutdown(&Shutdown);
    }
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&s.params);
    if (NVSDK_NGX_FAILED(r) || s.params == NULL) {
        LOGW << "DLSS: no NGX capability parameters (0x" << std::hex << (unsigned)r << std::dec << ")" << std::endl;
        Shutdown();
        s.tried = true;
        return false;
    }
    int available = 0, needs_driver = 0, init_result = 0;
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available) {
        NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
        NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
        LOGW << "DLSS: not available here (init result 0x" << std::hex << (unsigned)init_result << std::dec
             << (needs_driver ? ", needs a newer driver" : "") << "; is nvngx_dlss.dll beside the game?)" << std::endl;
        Shutdown();
        s.tried = true;
        return false;
    }
    s.available = true;
    LOGI << "DLSS: available (NGX started)" << std::endl;
    return true;
}

NVSDK_NGX_Resource_VK Wrap(const vkgl::ImageInfo& info, bool depth, bool writable) {
    VkImageSubresourceRange range = {depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(info.view, info.image, range, info.format, info.width, info.height, writable);
}

bool CreateFeature(VkCommandBuffer cmd, const DlssFrame& f) {
    ReleaseFeature();
    // The model for each mode (0: NGX's choice, which may change with its updates)
    const char* const presets[] = {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                   NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                   NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance};
    for (const char* p : presets) {
        NVSDK_NGX_Parameter_SetUI(s.params, p, (unsigned)f.preset);
    }
    NVSDK_NGX_DLSS_Create_Params create;
    memset(&create, 0, sizeof(create));
    create.Feature.InWidth = f.render_width;
    create.Feature.InHeight = f.render_height;
    create.Feature.InTargetWidth = f.output_width;
    create.Feature.InTargetHeight = f.output_height;
    create.Feature.InPerfQualityValue = (NVSDK_NGX_PerfQuality_Value)f.quality;
    // HDR in (linear, before the tone map), motion at the render size and without the
    // jitter, reversed-Z depth, exposure DLSS's own
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                  NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT1(VkBackend::Device(), cmd, 1, 1, &s.feature, s.params, &create);
    if (NVSDK_NGX_FAILED(r) || s.feature == NULL) {
        LOGE << "DLSS: couldn't make the " << f.render_width << "x" << f.render_height << " -> " << f.output_width << "x" << f.output_height
             << " feature (0x" << std::hex << (unsigned)r << std::dec << "): off" << std::endl;
        s.feature = NULL;
        s.available = false;
        return false;
    }
    s.width = f.render_width;
    s.height = f.render_height;
    s.out_width = f.output_width;
    s.out_height = f.output_height;
    s.quality = f.quality;
    s.preset = f.preset;
    LOGI << "DLSS: " << f.render_width << "x" << f.render_height << " -> " << f.output_width << "x" << f.output_height << ", quality "
         << f.quality << ", preset " << f.preset << std::endl;
    return true;
}

}  // namespace

void AppendInstanceExtensions(std::vector<const char*>* extensions) {
    const NVSDK_NGX_FeatureDiscoveryInfo info = Discovery();
    uint32_t count = 0;
    VkExtensionProperties* needed = NULL;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &needed)) || needed == NULL) {
        return;
    }
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    std::vector<VkExtensionProperties> have(n);
    vkEnumerateInstanceExtensionProperties(NULL, &n, have.data());
    s.instance_names.clear();  // (a new instance's; reserved: the pointers handed out stay)
    s.instance_names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = needed[i].extensionName;
        bool present = false;
        for (const VkExtensionProperties& h : have) {
            present = present || strcmp(h.extensionName, name) == 0;
        }
        if (present && !Has(*extensions, name)) {
            s.instance_names.push_back(name);
            extensions->push_back(s.instance_names.back().c_str());
            LOGI << "DLSS: instance extension " << name << std::endl;
        }
    }
}

void AppendDeviceExtensions(VkInstance instance, VkPhysicalDevice physical, const std::vector<VkExtensionProperties>& available,
                            std::vector<const char*>* extensions) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(physical, &p);
    if (p.vendorID != 0x10DE) {
        return;
    }
    const NVSDK_NGX_FeatureDiscoveryInfo info = Discovery();
    uint32_t count = 0;
    VkExtensionProperties* needed = NULL;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &info, &count, &needed)) || needed == NULL) {
        return;
    }
    s.device_names.clear();
    s.device_names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = needed[i].extensionName;
        bool present = false;
        for (const VkExtensionProperties& h : available) {
            present = present || strcmp(h.extensionName, name) == 0;
        }
        if (!present) {
            LOGW << "DLSS: the device has no " << name << std::endl;
        } else if (!Has(*extensions, name)) {
            s.device_names.push_back(name);
            extensions->push_back(s.device_names.back().c_str());
            LOGI << "DLSS: device extension " << name << std::endl;
        }
    }
}

bool Requested() {
    const int mode = Graphics::TemporalModeFromConfig(config);
    return mode >= 2 && mode <= 6;  // (Temporal::kDLAA .. kDLSSUltraPerformance)
}

bool DlssAvailable() {
    if (!s.tried && VkBackend::Ready()) {
        Start();
    }
    return s.available;
}

bool EvaluateDlss(const DlssFrame& f) {
    if (!DlssAvailable()) {
        return false;
    }
    const GLuint names[4] = {f.color, f.depth, f.motion, f.output};
    vkgl::ImageInfo img[4];
    int begun = 0;
    for (; begun < 4; ++begun) {
        if (!vkgl::BeginExternal(names[begun], VK_IMAGE_LAYOUT_GENERAL, &img[begun])) {
            break;
        }
    }
    bool ok = begun == 4;
    if (ok) {
        VkCommandBuffer cmd = VkBackend::TransferCommands();
        bool reset = f.reset;
        if (s.feature == NULL || s.width != f.render_width || s.height != f.render_height || s.out_width != f.output_width ||
            s.out_height != f.output_height || s.quality != f.quality || s.preset != f.preset) {
            ok = CreateFeature(cmd, f);
            reset = true;
        }
        if (ok) {
            NVSDK_NGX_Resource_VK color = Wrap(img[0], false, false);
            NVSDK_NGX_Resource_VK depth = Wrap(img[1], true, false);
            NVSDK_NGX_Resource_VK motion = Wrap(img[2], false, false);
            NVSDK_NGX_Resource_VK output = Wrap(img[3], false, true);
            NVSDK_NGX_VK_DLSS_Eval_Params eval;
            memset(&eval, 0, sizeof(eval));
            eval.Feature.pInColor = &color;
            eval.Feature.pInOutput = &output;
            eval.pInDepth = &depth;
            eval.pInMotionVectors = &motion;
            eval.InJitterOffsetX = f.jitter_x;
            eval.InJitterOffsetY = f.jitter_y;
            eval.InRenderSubrectDimensions.Width = f.render_width;
            eval.InRenderSubrectDimensions.Height = f.render_height;
            eval.InReset = reset ? 1 : 0;
            eval.InMVScaleX = f.motion_scale_x;
            eval.InMVScaleY = f.motion_scale_y;
            eval.InPreExposure = 1.0f;
            eval.InExposureScale = 1.0f;
            const NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, s.feature, s.params, &eval);
            if (NVSDK_NGX_FAILED(r)) {
                LOGE << "DLSS: evaluate failed (0x" << std::hex << (unsigned)r << std::dec << "): off" << std::endl;
                ReleaseFeature();
                s.available = false;
                ok = false;
            }
        }
    }
    for (int i = 0; i < begun; ++i) {
        vkgl::EndExternal(names[i]);
    }
    return ok;
}

}  // namespace VkNgx

#else  // OG_DLSS

namespace VkNgx {

bool Requested() {
    return false;
}

void AppendInstanceExtensions(std::vector<const char*>* extensions) {
    (void)extensions;
}

void AppendDeviceExtensions(VkInstance instance, VkPhysicalDevice physical, const std::vector<VkExtensionProperties>& available,
                            std::vector<const char*>* extensions) {
    (void)instance;
    (void)physical;
    (void)available;
    (void)extensions;
}

bool DlssAvailable() {
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGI << "DLSS: not in this build (CMake's DLSS_SDK)" << std::endl;
    }
    return false;
}

bool EvaluateDlss(const DlssFrame& frame) {
    (void)frame;
    return false;
}

}  // namespace VkNgx

#endif  // OG_DLSS
