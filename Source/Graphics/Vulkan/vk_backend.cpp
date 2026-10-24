//-----------------------------------------------------------------------------
//           Name: vk_backend.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The Vulkan 1.3 renderer's core: see vk_backend.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>
#include <vk_mem_alloc.h>

#include "vk_backend.h"
#include "vk_ngx.h"

#include <Logging/logdata.h>
#include <Internal/error.h>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>
#include <deque>
#include <functional>
#include <vector>

namespace VkBackend {
namespace {

const int kFramesInFlight = 2;

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> spare;     // made in the pool, free again after its reset
    size_t used = 0;                        // spare[0, used) recorded in this frame
    uint64_t last_serial = 0;               // the frame's last submit: the pool is reset once it's done
    VkSemaphore acquired = VK_NULL_HANDLE;  // its swapchain image may be written
};

struct Deferred {
    uint64_t serial;  // destroyed once the GPU has finished this submit
    std::function<void()> destroy;
};

struct State {
    SDL_Window* window = NULL;
    bool ready = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool labels = false;  // VK_EXT_debug_utils enabled: labels for capture tools
    std::function<void()> on_fatal;
    // Vsync pacing (VK_KHR_present_id + VK_KHR_present_wait): each present waits until the
    // frame before it is on screen, as GL's swap blocks. Without it the GPU was held back
    // only by the display handing images back in bursts: a run of fast frames, then a
    // long one, every 7-9 frames
    bool present_wait = false;
    uint64_t present_id = 0;
    uint64_t swapchain_first_id = 1;  // (the first id presented on the current swapchain)
    FrameDiag diag, last_diag;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties = {};
    uint32_t queue_family = 0;
    uint32_t timestamp_bits = 0;  // the queue's timestampValidBits
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> images;
    std::vector<VkSemaphore> rendered;  // per swapchain image: its commands are done, the present may read it
    bool vsync = true;
    bool swapchain_stale = false;  // made again before the next present
    int drawable[2] = {0, 0};      // the window's size it was made for

    Frame frames[kFramesInFlight];
    int frame_index = 0;
    VkCommandBuffer cmd = VK_NULL_HANDLE;  // being recorded (from the frame's pool)
    bool recorded = false;                 // anything recorded into it (Commands() was asked)
    VkPhysicalDeviceFeatures features = {};  // enabled
    uint32_t max_push_descriptors = 0;
    VkResolveModeFlags depth_resolve_modes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    // Every submit signals the next value of the timeline: the "serial" that the
    // commands being recorded will finish with is submitted + 1
    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t submitted = 0;
    uint64_t completed = 0;  // (cached)
    std::deque<Deferred> deferred;
    std::vector<std::function<void()>> before_submit;
    std::vector<std::function<void()>> before_shutdown;
    std::vector<std::function<void()>> frame_begin;
    std::function<void()> before_transfer;  // (the passes: end the one being drawn)
    uint64_t presented = 0;
    std::string description;
};

State s;

// The renderer gives up: the next start is OpenGL's (OnFatal), then the game ends
void Fatal(const char* what, const char* arg) {
    if (s.on_fatal) {
        std::function<void()> before = std::move(s.on_fatal);
        s.on_fatal = nullptr;
        before();
    }
    char buf[512];
    snprintf(buf, sizeof(buf), what, arg);
    FatalError("Error", "Vulkan: %s. The game will start with OpenGL next time (Settings, Graphics, Renderer chooses again).", buf);
}
int load_result = -1;  // LoadVulkan: -1 not tried, 0 failed, 1 loaded
std::atomic<int> validation_problems(0);

const char* ResultString(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult";
    }
}

#define VK_CHECK(call)                                                                       \
    do {                                                                                     \
        VkResult vk_check_result = (call);                                                   \
        if (vk_check_result != VK_SUCCESS) {                                                 \
            LOGE << "Vulkan: " #call ": " << ResultString(vk_check_result) << " (" << (int)vk_check_result << ")" << std::endl; \
            return false;                                                                    \
        }                                                                                    \
    } while (0)

std::string VersionString(uint32_t v) {
    std::ostringstream oss;
    oss << VK_API_VERSION_MAJOR(v) << "." << VK_API_VERSION_MINOR(v) << "." << VK_API_VERSION_PATCH(v);
    return oss.str();
}

// Drivers pack their versions differently (NVIDIA 10.8.8.6 bits, Intel on Windows 18.14)
std::string DriverVersionString(const VkPhysicalDeviceProperties& p) {
    uint32_t v = p.driverVersion;
    std::ostringstream oss;
    if (p.vendorID == 0x10DE) {
        oss << ((v >> 22) & 0x3ff) << "." << ((v >> 14) & 0xff);
    } else if (p.vendorID == 0x8086) {
        oss << (v >> 14) << "." << (v & 0x3fff);
    } else {
        oss << VersionString(v);
    }
    return oss.str();
}

const char* DeviceTypeString(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
        default: return "other";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types;
    (void)user;
    // The loader's own messages are about other software's layers on the PC
    // (overlays, ReShade): logged, not counted
    if (data->pMessageIdName != NULL && strcmp(data->pMessageIdName, "Loader Message") == 0) {
        LOGW << "Vulkan loader: " << (data->pMessage ? data->pMessage : "") << std::endl;
        return VK_FALSE;
    }
    // What the GL engine does by design, and GL leaves undefined just the same: depth-only
    // programs that declare a colour output, drawn into depth-only passes or with a colour
    // target they don't write. Logged once each, not counted
    if (data->pMessageIdName != NULL && (strcmp(data->pMessageIdName, "Undefined-Value-ShaderOutputNotConsumed-DynamicRendering") == 0 ||
                                         strcmp(data->pMessageIdName, "Undefined-Value-OutputNotWritten-DynamicRendering") == 0)) {
        static std::atomic<int> seen_not_consumed(0), seen_not_written(0);
        std::atomic<int>& seen = data->pMessageIdName[16] == 'S' ? seen_not_consumed : seen_not_written;
        if (seen++ == 0) {
            LOGI << "Vulkan (known, from GL's engine): " << (data->pMessage ? data->pMessage : "") << std::endl;
        }
        return VK_FALSE;
    }
    // Overgrowth Online (TAA): NGX's own driver calls (DLSS's kernels, VK_NVX_binary_import)
    // with structs newer than the layer knows. Logged once, not counted
    // ... and FSR's (FidelityFX's) and NGX's shaders' storage images, whose declared formats
    // aren't those of the images they're given: ours use no storage images
    if (data->pMessageIdName != NULL && strcmp(data->pMessageIdName, "Undefined-Value-StorageImage-FormatMismatch-ImageView") == 0) {
        static std::atomic<int> seen_storage(0);
        if (seen_storage++ == 0) {
            LOGW << "Vulkan (FSR's or NGX's shaders; not counted): " << (data->pMessage ? data->pMessage : "") << std::endl;
        }
        return VK_FALSE;
    }
    if (data->pMessageIdName != NULL && strncmp(data->pMessageIdName, "VUID-VkCu", 9) == 0) {
        static std::atomic<int> seen_ngx(0);
        if (seen_ngx++ == 0) {
            LOGW << "Vulkan (NGX's, DLSS; not counted): " << (data->pMessage ? data->pMessage : "") << std::endl;
        }
        return VK_FALSE;
    }
    // Another program's code in the game (an overlay hooked into the present, as RivaTuner's
    // is, draws with render pass objects; the renderer never does: dynamic rendering only).
    // Logged once, not counted
    if (data->pMessage != NULL && (strstr(data->pMessage, "vkCmdEndRenderPass") != NULL || strstr(data->pMessage, "vkCmdBeginRenderPass") != NULL ||
                                   strstr(data->pMessage, "VkRenderPass ") != NULL)) {
        static std::atomic<int> seen_other(0);
        if (seen_other++ == 0) {
            LOGW << "Vulkan (another program's code in the game, e.g. an overlay; not counted): " << data->pMessage << std::endl;
        }
        return VK_FALSE;
    }
    const int n = ++validation_problems;
    if (n <= 100) {
        LOGE << "Vulkan " << ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning") << " "
             << (data->pMessageIdName ? data->pMessageIdName : "") << ": " << (data->pMessage ? data->pMessage : "") << std::endl;
    } else if (n == 101) {
        LOGE << "Vulkan: more validation messages, counted but not logged" << std::endl;
    }
    return VK_FALSE;
}

bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list) {
        if (strcmp(e.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool CreateInstance(int validation) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != NULL) {
        vkEnumerateInstanceVersion(&loader_version);
    }
    if (loader_version < VK_API_VERSION_1_3) {
        LOGI << "Vulkan loader " << VersionString(loader_version) << " is older than 1.3" << std::endl;
        return false;
    }

    unsigned count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(s.window, &count, NULL)) {
        LOGE << "Vulkan: SDL_Vulkan_GetInstanceExtensions: " << SDL_GetError() << std::endl;
        return false;
    }
    std::vector<const char*> extensions(count);
    SDL_Vulkan_GetInstanceExtensions(s.window, &count, extensions.data());

    std::vector<const char*> layers;
    bool debug_utils = false;
    bool validation_features = false;
    if (validation > 0) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, NULL);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        bool found = false;
        for (const VkLayerProperties& p : props) {
            found = found || strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        }
        if (found) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            LOGW << "Vulkan validation asked for, but VK_LAYER_KHRONOS_validation isn't installed (the Vulkan SDK has it)" << std::endl;
        }
        uint32_t e = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &e, NULL);
        std::vector<VkExtensionProperties> instance_extensions(e);
        vkEnumerateInstanceExtensionProperties(NULL, &e, instance_extensions.data());
        if (HasExtension(instance_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug_utils = true;
        }
        // (and with DLSS: see the features below)
        if (found && (validation > 1 || VkNgx::Requested())) {
            // (the layer's own extension)
            uint32_t le = 0;
            vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &le, NULL);
            std::vector<VkExtensionProperties> layer_extensions(le);
            vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &le, layer_extensions.data());
            if (HasExtension(layer_extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
                extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                validation_features = true;
            }
        }
    }

    // (labels for capture tools: the extension whenever the instance has it, the
    // messenger only with validation)
    s.labels = debug_utils;
    if (!s.labels) {
        uint32_t e = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &e, NULL);
        std::vector<VkExtensionProperties> instance_extensions(e);
        vkEnumerateInstanceExtensionProperties(NULL, &e, instance_extensions.data());
        if (HasExtension(instance_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            s.labels = true;
        }
    }

    VkNgx::AppendInstanceExtensions(&extensions);  // Overgrowth Online (TAA): DLAA/DLSS's

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Overgrowth Online";
    app.applicationVersion = 1;
    app.pEngineName = "Phoenix";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messenger_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = &DebugCallback;

    const VkValidationFeatureEnableEXT sync_validation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features = {VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    features.enabledValidationFeatureCount = validation > 1 ? 1 : 0;
    features.pEnabledValidationFeatures = &sync_validation;
    // Overgrowth Online (TAA): DLSS (NGX) hands image views' driver handles to its own
    // kernels; the layer's wrapped handles reach the driver there and crash it
    const VkValidationFeatureDisableEXT unique_handles = VK_VALIDATION_FEATURE_DISABLE_UNIQUE_HANDLES_EXT;
    if (VkNgx::Requested()) {
        features.disabledValidationFeatureCount = 1;
        features.pDisabledValidationFeatures = &unique_handles;
    }

    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = (uint32_t)extensions.size();
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = (uint32_t)layers.size();
    info.ppEnabledLayerNames = layers.data();
    const void** next = &info.pNext;
    if (debug_utils) {  // (also what instance creation itself reports)
        *next = &messenger_info;
        next = &messenger_info.pNext;
    }
    if (validation_features) {
        *next = &features;
    }
    VK_CHECK(vkCreateInstance(&info, NULL, &s.instance));
    volkLoadInstance(s.instance);
    messenger_info.pNext = NULL;
    if (debug_utils) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(s.instance, &messenger_info, NULL, &s.messenger));
    }
    LOGI << "Vulkan instance: loader " << VersionString(loader_version) << (layers.empty() ? "" : ", validation layer")
         << (validation_features && validation > 1 ? " with synchronization checks" : "")
         << (validation_features && VkNgx::Requested() ? " (no handle wrapping: DLSS)" : "") << std::endl;
    return true;
}

bool CreateSurface() {
    if (!SDL_Vulkan_CreateSurface(s.window, s.instance, &s.surface)) {
        LOGE << "Vulkan: SDL_Vulkan_CreateSurface: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

// A device with Vulkan 1.3, dynamic rendering, synchronization2, a swapchain and a
// queue that draws and presents; discrete GPUs first
bool PickDevice() {
    uint32_t n = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(s.instance, &n, NULL));
    std::vector<VkPhysicalDevice> devices(n);
    VK_CHECK(vkEnumeratePhysicalDevices(s.instance, &n, devices.data()));
    int best_score = -1;
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDevice d = devices[i];
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        std::string missing;
        uint32_t family = UINT32_MAX;
        if (p.apiVersion < VK_API_VERSION_1_3) {
            missing = "Vulkan 1.3";
        } else {
            VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext = &f13;
            vkGetPhysicalDeviceFeatures2(d, &f2);
            uint32_t e = 0;
            vkEnumerateDeviceExtensionProperties(d, NULL, &e, NULL);
            std::vector<VkExtensionProperties> extensions(e);
            vkEnumerateDeviceExtensionProperties(d, NULL, &e, extensions.data());
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, NULL);
            std::vector<VkQueueFamilyProperties> queues(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, queues.data());
            for (uint32_t q = 0; q < qn && family == UINT32_MAX; ++q) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, q, s.surface, &present);
                if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT) && present) {
                    family = q;
                }
            }
            if (!f13.dynamicRendering || !f13.synchronization2) {
                missing = "dynamic rendering and synchronization2";
            } else if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                missing = "a swapchain";
            } else if (!HasExtension(extensions, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
                missing = "push descriptors";  // (every program's textures and blocks, stage 5.4)
            } else if (family == UINT32_MAX) {
                missing = "a queue that draws and presents to this window";
            }
        }
        LOGI << "Vulkan device " << i << ": " << p.deviceName << " (" << DeviceTypeString(p.deviceType) << "), Vulkan "
             << VersionString(p.apiVersion) << ", driver " << DriverVersionString(p) << (missing.empty() ? "" : ": no " + missing) << std::endl;
        if (!missing.empty()) {
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > best_score) {
            best_score = score;
            s.physical = d;
            s.properties = p;
            s.queue_family = family;
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, NULL);
            std::vector<VkQueueFamilyProperties> queues(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, queues.data());
            s.timestamp_bits = family < qn ? queues[family].timestampValidBits : 0;
        }
    }
    if (s.physical == VK_NULL_HANDLE) {
        LOGI << "No Vulkan 1.3 device that can run the game" << std::endl;
        return false;
    }
    std::ostringstream oss;
    oss << s.properties.deviceName << ", driver " << DriverVersionString(s.properties) << ", Vulkan " << VersionString(s.properties.apiVersion);
    s.description = oss.str();
    return true;
}

bool CreateDevice() {
    // What later stages draw with, where the device has it (5.3-5.6 check what they need)
    VkPhysicalDeviceFeatures have = {};
    vkGetPhysicalDeviceFeatures(s.physical, &have);
    VkPhysicalDeviceFeatures want = {};
    want.samplerAnisotropy = have.samplerAnisotropy;
    want.textureCompressionBC = have.textureCompressionBC;
    want.imageCubeArray = have.imageCubeArray;
    want.independentBlend = have.independentBlend;
    want.fillModeNonSolid = have.fillModeNonSolid;  // (editor wireframe)
    want.wideLines = have.wideLines;
    want.largePoints = have.largePoints;
    want.depthClamp = have.depthClamp;
    want.depthBiasClamp = have.depthBiasClamp;
    want.sampleRateShading = have.sampleRateShading;
    want.shaderClipDistance = have.shaderClipDistance;
    want.occlusionQueryPrecise = have.occlusionQueryPrecise;
    want.multiDrawIndirect = have.multiDrawIndirect;
    want.drawIndirectFirstInstance = have.drawIndirectFirstInstance;
    want.geometryShader = have.geometryShader;
    // Overgrowth Online (FSR): FidelityFX picks its 16-bit shaders by what the device
    // has, not by what's enabled: on where it has them
    want.shaderInt16 = have.shaderInt16;

    VkPhysicalDeviceVulkan11Features have11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features have12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features have13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 have2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have2.pNext = &have11;
    have11.pNext = &have12;
    have12.pNext = &have13;
    vkGetPhysicalDeviceFeatures2(s.physical, &have2);

    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    // Overgrowth Online (FSR): its wave sizes, where the device lets them be chosen
    f13.subgroupSizeControl = have13.subgroupSizeControl;
    f13.computeFullSubgroups = have13.computeFullSubgroups;
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;  // (required in 1.2: every submit numbered, stage 5.3)
    f12.hostQueryReset = have12.hostQueryReset;
    // Overgrowth Online (TAA): NGX's buffers (DLSS) take device addresses where the device has them
    f12.bufferDeviceAddress = have12.bufferDeviceAddress;
    // Overgrowth Online (FSR): its 16-bit shaders and descriptor arrays, as above
    f12.shaderFloat16 = have12.shaderFloat16;
    f12.shaderStorageBufferArrayNonUniformIndexing = have12.shaderStorageBufferArrayNonUniformIndexing;
    f12.shaderSampledImageArrayNonUniformIndexing = have12.shaderSampledImageArrayNonUniformIndexing;
    f12.shaderStorageImageArrayNonUniformIndexing = have12.shaderStorageImageArrayNonUniformIndexing;
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &f12;
    f11.shaderDrawParameters = have11.shaderDrawParameters;
    f11.storageBuffer16BitAccess = have11.storageBuffer16BitAccess;  // (Overgrowth Online FSR)
    f11.uniformAndStorageBuffer16BitAccess = have11.uniformAndStorageBuffer16BitAccess;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f11;
    f2.features = want;
    s.features = want;

    uint32_t e = 0;
    vkEnumerateDeviceExtensionProperties(s.physical, NULL, &e, NULL);
    std::vector<VkExtensionProperties> available(e);
    vkEnumerateDeviceExtensionProperties(s.physical, NULL, &e, available.data());
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    // (vsync pacing, where the device has it: see State::present_wait)
    VkPhysicalDevicePresentIdFeaturesKHR present_id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    if (HasExtension(available, VK_KHR_PRESENT_ID_EXTENSION_NAME) && HasExtension(available, VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 have_present = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        have_present.pNext = &present_id;
        present_id.pNext = &present_wait;
        vkGetPhysicalDeviceFeatures2(s.physical, &have_present);
        present_id.pNext = NULL;
        if (present_id.presentId && present_wait.presentWait) {
            extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
            f13.pNext = &present_id;
            present_id.pNext = &present_wait;
            s.present_wait = true;
        }
    }
    VkNgx::AppendDeviceExtensions(s.instance, s.physical, available, &extensions);  // Overgrowth Online (TAA): DLAA/DLSS's
    // Overgrowth Online (FSR): FidelityFX asks for vkGetBufferMemoryRequirements2 by its KHR
    // name, which the device only answers with the (core since 1.1) extension enabled
    if (HasExtension(available, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    }
    VkPhysicalDevicePushDescriptorPropertiesKHR push = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
    VkPhysicalDeviceDepthStencilResolveProperties resolve = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    push.pNext = &resolve;
    VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &push;
    vkGetPhysicalDeviceProperties2(s.physical, &props2);
    s.max_push_descriptors = push.maxPushDescriptors;
    s.depth_resolve_modes = resolve.supportedDepthResolveModes | VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = s.queue_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &f2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = (uint32_t)extensions.size();
    info.ppEnabledExtensionNames = extensions.data();
    VK_CHECK(vkCreateDevice(s.physical, &info, NULL, &s.device));
    volkLoadDevice(s.device);
    vkGetDeviceQueue(s.device, s.queue_family, 0, &s.queue);

    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator_info.instance = s.instance;
    allocator_info.physicalDevice = s.physical;
    allocator_info.device = s.device;
    VmaVulkanFunctions functions = {};
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocator_info, &functions));
    allocator_info.pVulkanFunctions = &functions;
    VK_CHECK(vmaCreateAllocator(&allocator_info, &s.allocator));
    return true;
}

void DestroySwapchainParts() {
    for (VkSemaphore sem : s.rendered) {
        vkDestroySemaphore(s.device, sem, NULL);
    }
    s.rendered.clear();
    s.images.clear();
}

const char* PresentModeString(VkPresentModeKHR m) {
    switch (m) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
        case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo relaxed";
        default: return "other";
    }
}

// (Again, after a resize, a vsync change, fullscreen or alt-tab.) A minimised window
// has no swapchain until it has a size again
bool CreateSwapchain() {
    vkDeviceWaitIdle(s.device);
    s.swapchain_stale = false;
    SDL_Vulkan_GetDrawableSize(s.window, &s.drawable[0], &s.drawable[1]);
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s.physical, s.surface, &caps));
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(s.window, &w, &h);
        extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, (uint32_t)std::max(w, 0)));
        extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, (uint32_t)std::max(h, 0)));
    }
    VkSwapchainKHR old = s.swapchain;
    DestroySwapchainParts();
    if (extent.width == 0 || extent.height == 0) {
        if (old != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(s.device, old, NULL);
        }
        s.swapchain = VK_NULL_HANDLE;
        s.extent = extent;
        return true;
    }

    uint32_t n = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical, s.surface, &n, NULL));
    std::vector<VkSurfaceFormatKHR> formats(n);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical, s.surface, &n, formats.data()));
    // (UNORM: the post chain writes gamma-encoded values, as into GL's window)
    VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
        bool found = false;
        for (const VkSurfaceFormatKHR& f : formats) {
            if (f.format == wanted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                format = f;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }

    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &n, NULL));
    std::vector<VkPresentModeKHR> modes(n);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &n, modes.data()));
    auto has_mode = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    // (like GL's swap intervals: -1 when the driver has it, else 1; vsync off is 0)
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (s.vsync) {
        if (has_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) {
            mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
    } else if (has_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    } else if (has_mode(VK_PRESENT_MODE_MAILBOX_KHR)) {
        mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }
    const VkImageUsageFlags wanted_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        LOGE << "Vulkan: the swapchain can't be a transfer destination" << std::endl;
        return false;
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & alpha) == 0) {
        for (uint32_t bit = 1; bit != 0; bit <<= 1) {
            if (caps.supportedCompositeAlpha & bit) {
                alpha = (VkCompositeAlphaFlagBitsKHR)bit;
                break;
            }
        }
    }

    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = s.surface;
    info.minImageCount = image_count;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = wanted_usage & caps.supportedUsageFlags;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    VkResult r = vkCreateSwapchainKHR(s.device, &info, NULL, &s.swapchain);
    if (old != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(s.device, old, NULL);
    }
    if (r != VK_SUCCESS) {
        s.swapchain = VK_NULL_HANDLE;
        LOGE << "Vulkan: vkCreateSwapchainKHR: " << ResultString(r) << std::endl;
        return false;
    }
    s.extent = extent;
    s.swapchain_first_id = s.present_id + 1;  // (present ids go on rising; the wait is per swapchain)

    VK_CHECK(vkGetSwapchainImagesKHR(s.device, s.swapchain, &n, NULL));
    s.images.resize(n);
    VK_CHECK(vkGetSwapchainImagesKHR(s.device, s.swapchain, &n, s.images.data()));
    s.rendered.resize(n, VK_NULL_HANDLE);
    for (VkSemaphore& sem : s.rendered) {
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(s.device, &si, NULL, &sem));
    }
    LOGI << "Vulkan swapchain: " << extent.width << "x" << extent.height << ", " << n << " images, format " << (int)format.format
         << ", " << PresentModeString(mode) << (s.present_wait && s.vsync ? ", paced by present wait" : "") << std::endl;
    return true;
}

bool CreateFrames() {
    for (Frame& f : s.frames) {
        VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = s.queue_family;
        VK_CHECK(vkCreateCommandPool(s.device, &pool, NULL, &f.pool));
        VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(s.device, &sem, NULL, &f.acquired));
    }
    VkSemaphoreTypeCreateInfo type = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    sem.pNext = &type;
    VK_CHECK(vkCreateSemaphore(s.device, &sem, NULL, &s.timeline));
    return true;
}

uint64_t QueryCompleted() {
    uint64_t value = s.completed;
    if (vkGetSemaphoreCounterValue(s.device, s.timeline, &value) == VK_SUCCESS) {
        s.completed = std::max(s.completed, value);
    }
    return s.completed;
}

void RunDeferred() {
    const uint64_t done = QueryCompleted();
    while (!s.deferred.empty() && s.deferred.front().serial <= done) {
        std::function<void()> destroy = std::move(s.deferred.front().destroy);
        s.deferred.pop_front();
        destroy();
    }
}

// A new command buffer from the frame's pool, recording
bool BeginCommandBuffer() {
    Frame& f = s.frames[s.frame_index];
    if (f.used == f.spare.size()) {
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = f.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(s.device, &alloc, &cmd));
        f.spare.push_back(cmd);
    }
    s.cmd = f.spare[f.used++];
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(s.cmd, &begin));
    return true;
}

// Waits until the GPU is done with the last frame that used this slot, then opens
// a command buffer from its pool
bool BeginFrame() {
    Frame& f = s.frames[s.frame_index];
    if (f.last_serial > QueryCompleted()) {
        VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &s.timeline;
        wait.pValues = &f.last_serial;
        const auto start = std::chrono::steady_clock::now();
        VK_CHECK(vkWaitSemaphores(s.device, &wait, UINT64_MAX));
        s.diag.slot_wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    VK_CHECK(vkResetCommandPool(s.device, f.pool, 0));
    f.used = 0;
    RunDeferred();
    if (!BeginCommandBuffer()) {
        return false;
    }
    for (std::function<void()>& fn : s.frame_begin) {
        fn();
    }
    return true;
}

// Ends the command buffer being recorded and submits it as the next serial, with the
// swapchain's semaphores when it presents
void Submit(VkSemaphore wait_acquired, VkSemaphore signal_rendered) {
    if (s.before_transfer) {
        s.before_transfer();  // (no pass is left open across a submit)
    }
    for (std::function<void()>& f : s.before_submit) {
        f();
    }
    ++s.diag.submits;
    vkEndCommandBuffer(s.cmd);
    const uint64_t serial = s.submitted + 1;
    VkCommandBufferSubmitInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = s.cmd;
    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = wait_acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;  // (what writes the swapchain image waits on it)
    VkSemaphoreSubmitInfo signals[2] = {{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    signals[0].semaphore = s.timeline;
    signals[0].value = serial;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].semaphore = signal_rendered;
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = wait_acquired != VK_NULL_HANDLE ? 1 : 0;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    submit.signalSemaphoreInfoCount = signal_rendered != VK_NULL_HANDLE ? 2 : 1;
    submit.pSignalSemaphoreInfos = signals;
    VkResult r = vkQueueSubmit2(s.queue, 1, &submit, VK_NULL_HANDLE);
    if (r != VK_SUCCESS) {
        LOGE << "Vulkan: vkQueueSubmit2: " << ResultString(r) << std::endl;
        Fatal("the GPU stopped taking work (%s)", ResultString(r));
    }
    s.submitted = serial;
    s.frames[s.frame_index].last_serial = serial;
    s.cmd = VK_NULL_HANDLE;
    s.recorded = false;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkImageLayout old_layout,
                  VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout new_layout) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo d = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &d);
}

}  // namespace

bool LoadVulkan() {
    if (load_result == -1) {
        VkResult r = volkInitialize();
        load_result = r == VK_SUCCESS ? 1 : 0;
        if (r != VK_SUCCESS) {
            LOGI << "No Vulkan on this PC (vulkan-1.dll): " << ResultString(r) << std::endl;
        }
    }
    return load_result == 1;
}

bool Init(SDL_Window* window, bool vsync, int validation) {
    if (!LoadVulkan()) {
        return false;
    }
    s.window = window;
    s.vsync = vsync;
    if (!CreateInstance(validation) || !CreateSurface() || !PickDevice() || !CreateDevice() || !CreateSwapchain() || !CreateFrames() ||
        !BeginFrame()) {
        LOGE << "Vulkan didn't start: the game runs on OpenGL" << std::endl;
        Shutdown();
        return false;
    }
    s.ready = true;
    LOGI << "Vulkan renderer on " << s.description << std::endl;
    return true;
}

void Shutdown() {
    if (s.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(s.device);
        for (std::function<void()>& f : s.before_shutdown) {
            f();
        }
        for (Deferred& d : s.deferred) {
            d.destroy();
        }
        s.deferred.clear();
        for (Frame& f : s.frames) {
            if (f.acquired != VK_NULL_HANDLE) vkDestroySemaphore(s.device, f.acquired, NULL);
            if (f.pool != VK_NULL_HANDLE) vkDestroyCommandPool(s.device, f.pool, NULL);  // (and its command buffers)
            f = Frame();
        }
        if (s.timeline != VK_NULL_HANDLE) {
            vkDestroySemaphore(s.device, s.timeline, NULL);
        }
        DestroySwapchainParts();
        if (s.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(s.device, s.swapchain, NULL);
        }
        if (s.allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(s.allocator);
        }
        vkDestroyDevice(s.device, NULL);
    }
    if (s.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(s.instance, s.surface, NULL);
    }
    if (s.messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(s.instance, s.messenger, NULL);
    }
    if (s.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(s.instance, NULL);
    }
    s = State();
}

bool Ready() {
    return s.ready;
}

void GetDrawableSize(SDL_Window* window, int* width, int* height) {
    SDL_Vulkan_GetDrawableSize(window, width, height);
}

void WindowSize(int* width, int* height) {
    *width = *height = 0;
    if (s.window != NULL) {
        SDL_Vulkan_GetDrawableSize(s.window, width, height);
    }
}

void SetVsync(bool vsync) {
    if (vsync != s.vsync) {
        s.vsync = vsync;
        s.swapchain_stale = true;
    }
}

void WindowResized() {
    s.swapchain_stale = true;
}

void Present(const float clear_color[4]) {
    Present(clear_color, std::function<void(VkImage, uint32_t, uint32_t)>());
}

void Present(const float clear_color[4], const std::function<void(VkImage, uint32_t, uint32_t)>& fill) {
    if (!s.ready) {
        return;
    }
    if (s.before_transfer) {
        s.before_transfer();  // (the last pass ends before the frame goes to the swapchain)
    }
    Frame& f = s.frames[s.frame_index];
    // Minimised: nothing is presented (SDL keeps the old size meanwhile). A swapchain
    // that had no size is made again once the window has one
    const bool minimized = (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MINIMIZED) != 0;
    int drawable[2] = {0, 0};  // (resizes, fullscreen and alt-tab change it; a driver needn't say so)
    SDL_Vulkan_GetDrawableSize(s.window, &drawable[0], &drawable[1]);
    if (drawable[0] != s.drawable[0] || drawable[1] != s.drawable[1] || s.swapchain == VK_NULL_HANDLE) {
        s.swapchain_stale = true;
    }
    if (s.swapchain_stale && !minimized) {
        if (!CreateSwapchain()) {
            Fatal("could not make the swapchain again%s", "");
        }
    }
    uint32_t image = 0;
    bool have_image = false;
    if (s.swapchain != VK_NULL_HANDLE && !minimized) {
        VkResult r = vkAcquireNextImageKHR(s.device, s.swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &image);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            have_image = true;
            s.swapchain_stale = r == VK_SUBOPTIMAL_KHR;
        } else if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            s.swapchain_stale = true;
        } else {
            LOGE << "Vulkan: vkAcquireNextImageKHR: " << ResultString(r) << std::endl;
        }
    }
    if (have_image) {
        // (stage 5.5: the finished frame, flipped, instead of a clear)
        VkImage target = s.images[image];
        ImageBarrier(s.cmd, target, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        if (fill) {
            fill(target, s.extent.width, s.extent.height);
        } else {
            VkClearColorValue color;
            memcpy(color.float32, clear_color, sizeof(color.float32));
            VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(s.cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        }
        ImageBarrier(s.cmd, target, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    Submit(have_image ? f.acquired : VK_NULL_HANDLE, have_image ? s.rendered[image] : VK_NULL_HANDLE);
    VkResult r = VK_SUCCESS;
    if (have_image) {
        VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &s.rendered[image];
        present.swapchainCount = 1;
        present.pSwapchains = &s.swapchain;
        present.pImageIndices = &image;
        const uint64_t id = ++s.present_id;
        VkPresentIdKHR present_id = {VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
        present_id.swapchainCount = 1;
        present_id.pPresentIds = &id;
        if (s.present_wait) {
            present.pNext = &present_id;
        }
        r = vkQueuePresentKHR(s.queue, &present);
        // Vsync: the frame two before this one on screen first (State::present_wait; one
        // frame queued besides, as GL drivers allow: a single slow frame doesn't miss a
        // refresh. At most 100 ms: a window being moved or covered may hold presents back)
        if (s.present_wait && s.vsync && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) && id > s.swapchain_first_id + 1) {
            const auto start = std::chrono::steady_clock::now();
            vkWaitForPresentKHR(s.device, s.swapchain, id - 2, 100000000ull);
            s.diag.slot_wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            s.swapchain_stale = true;
        } else if (r != VK_SUCCESS) {
            LOGE << "Vulkan: vkQueuePresentKHR: " << ResultString(r) << std::endl;
        }
        ++s.presented;
    }
    s.frame_index = (s.frame_index + 1) % kFramesInFlight;
    const bool begun = BeginFrame();
    s.last_diag = s.diag;  // (its slot wait included: this frame's present waited for it)
    s.diag = FrameDiag();
    if (!begun) {
        Fatal("could not begin a frame%s", "");
    }
}

uint64_t FramesPresented() {
    return s.presented;
}

VkInstance Instance() {
    return s.instance;
}

VkDevice Device() {
    return s.device;
}

VkPhysicalDevice PhysicalDevice() {
    return s.physical;
}

const VkPhysicalDeviceProperties& Properties() {
    return s.properties;
}

VmaAllocator Allocator() {
    return s.allocator;
}

VkCommandBuffer Commands() {
    s.recorded = true;
    return s.cmd;
}

VkCommandBuffer TransferCommands() {
    if (s.before_transfer) {
        s.before_transfer();
    }
    return Commands();
}

void SetBeforeTransfer(std::function<void()> before_transfer) {
    s.before_transfer = std::move(before_transfer);
}

const VkPhysicalDeviceFeatures& Features() {
    return s.features;
}

uint32_t MaxPushDescriptors() {
    return s.max_push_descriptors;
}

uint64_t FenceSerial() {
    return s.recorded ? s.submitted + 1 : s.submitted;
}

uint64_t RecordingSerial() {
    return s.submitted + 1;
}

uint64_t CompletedSerial() {
    return QueryCompleted();
}

void Flush() {
    if (!s.ready) {
        return;
    }
    Submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
    if (!BeginCommandBuffer()) {
        Fatal("could not begin a command buffer%s", "");
    }
}

bool WaitSerial(uint64_t serial, uint64_t timeout_ns) {
    if (serial <= QueryCompleted()) {
        return true;
    }
    if (serial > s.submitted) {
        Flush();  // (like GL_SYNC_FLUSH_COMMANDS_BIT: what it waits for must be on its way)
    }
    VkSemaphoreWaitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &s.timeline;
    wait.pValues = &serial;
    const auto start = std::chrono::steady_clock::now();
    VkResult r = vkWaitSemaphores(s.device, &wait, timeout_ns);
    ++s.diag.waits;
    s.diag.wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (r == VK_SUCCESS) {
        s.completed = std::max(s.completed, serial);
        return true;
    }
    if (r != VK_TIMEOUT) {
        LOGE << "Vulkan: vkWaitSemaphores: " << ResultString(r) << std::endl;
    }
    return false;
}

void DeferDestroy(std::function<void()> destroy) {
    s.deferred.push_back({RecordingSerial(), std::move(destroy)});
}

void OnSubmit(std::function<void()> before_submit) {
    s.before_submit.push_back(std::move(before_submit));
}

void OnShutdown(std::function<void()> before_shutdown) {
    s.before_shutdown.push_back(std::move(before_shutdown));
}

void OnFatal(std::function<void()> before_fatal) {
    s.on_fatal = std::move(before_fatal);
}

void OnFrameBegin(std::function<void()> frame_begin) {
    s.frame_begin.push_back(std::move(frame_begin));
}

int ValidationProblems() {
    return validation_problems;
}

std::string DeviceDescription() {
    return s.description;
}

uint32_t VendorID() {
    return s.properties.vendorID;
}

FrameDiag LastFrameDiag() {
    return s.last_diag;
}

uint32_t TimestampValidBits() {
    return s.timestamp_bits;
}

VkResolveModeFlags DepthResolveModes() {
    return s.depth_resolve_modes;
}

void BeginLabel(const char* name) {
    if (s.labels && vkCmdBeginDebugUtilsLabelEXT != NULL && s.cmd != VK_NULL_HANDLE) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        vkCmdBeginDebugUtilsLabelEXT(s.cmd, &label);
    }
}

void EndLabel() {
    if (s.labels && vkCmdEndDebugUtilsLabelEXT != NULL && s.cmd != VK_NULL_HANDLE) {
        vkCmdEndDebugUtilsLabelEXT(s.cmd);
    }
}

void InsertLabel(const char* name) {
    if (s.labels && vkCmdInsertDebugUtilsLabelEXT != NULL && s.cmd != VK_NULL_HANDLE) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        vkCmdInsertDebugUtilsLabelEXT(s.cmd, &label);
    }
}

std::string MemoryDescription() {
    if (s.allocator == VK_NULL_HANDLE) {
        return std::string();
    }
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(s.physical, &memory);
    std::vector<VmaBudget> budgets(memory.memoryHeapCount);
    vmaGetHeapBudgets(s.allocator, budgets.data());
    uint64_t usage = 0, budget = 0, ours = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            usage += budgets[i].usage;
            budget += budgets[i].budget;
            ours += budgets[i].statistics.blockBytes;
        }
    }
    std::ostringstream oss;
    oss << (budget > usage ? (budget - usage) >> 20 : 0) << " of " << (budget >> 20) << " MB free (device-local), " << (ours >> 20)
        << " MB ours";
    return oss.str();
}

}  // namespace VkBackend
