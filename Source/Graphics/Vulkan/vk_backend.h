//-----------------------------------------------------------------------------
//           Name: vk_backend.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The Vulkan 1.3 renderer's core (stage 5.2 of
//                 docs_gl46_plan.md): vulkan-1.dll loaded at run time (volk),
//                 the instance with the validation layer in test runs, the
//                 device and its queue, VMA, the swapchain (vsync modes, made
//                 again on resize, fullscreen and alt-tab) and the frames in
//                 flight. A frame's command buffer is open from one present to
//                 the next; Present() finishes it into the swapchain image
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct SDL_Window;

namespace VkBackend {

// Loads vulkan-1.dll. False: no Vulkan on this PC (start on GL)
bool LoadVulkan();

// The instance, device and swapchain for a window made with SDL_WINDOW_VULKAN.
// validation: 0 off, 1 the validation layer, 2 with synchronization checks too.
// False (logged, everything made so far undone): start on GL instead
bool Init(SDL_Window* window, bool vsync, int validation);
void Shutdown();
bool Ready();

// The window's size in pixels
void GetDrawableSize(SDL_Window* window, int* width, int* height);
void WindowSize(int* width, int* height);  // (the Vulkan window's)

void SetVsync(bool vsync);
void WindowResized();  // the swapchain is made again before the next present

// Ends the frame: its commands, then the swapchain image (until the passes draw:
// cleared to the colour given), presented; the next frame's command buffer opens
void Present(const float clear_color[4]);
#ifdef VK_DEFINE_HANDLE
// The same, with the frame written by fill(swapchain image in TRANSFER_DST, width, height)
void Present(const float clear_color[4], const std::function<void(VkImage, uint32_t, uint32_t)>& fill);
#endif
uint64_t FramesPresented();

// Validation layer warnings and errors so far (test runs: "vk_validation"), for
// the bridge's vk_errors test like gl_problems
int ValidationProblems();
std::string DeviceDescription();  // "NVIDIA GeForce ..., driver ..., Vulkan 1.x.y"
uint32_t VendorID();  // PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
uint32_t TimestampValidBits();  // the queue's (0: no timestamps)
// Labels in the command stream for capture tools (GL's debug groups), when the
// instance has VK_EXT_debug_utils
void BeginLabel(const char* name);
void EndLabel();
void InsertLabel(const char* name);
std::string MemoryDescription();  // (VMA's budget: "N of M MB free (device-local), K MB ours")
// (diagnostics, config debug_frame_log) Where the frame just presented waited: the
// submits it made, the waits for the GPU and their time, the wait for its frame slot
struct FrameDiag {
    uint32_t submits = 0, waits = 0;
    double wait_ms = 0.0, slot_wait_ms = 0.0;
};
FrameDiag LastFrameDiag();

// ---- for the resource layer (vk_gl.cpp) and the passes

#ifdef VK_DEFINE_HANDLE
VkInstance Instance();  // (Overgrowth Online TAA: NGX starts with it)
VkDevice Device();
VkPhysicalDevice PhysicalDevice();
const VkPhysicalDeviceProperties& Properties();
// The command buffer being recorded: transfers and passes go into it in the
// engine's (GL's) order
VkCommandBuffer Commands();
// For transfers (copies, blits, layout changes outside a pass): the pass being drawn
// ends first (SetBeforeTransfer)
VkCommandBuffer TransferCommands();
const VkPhysicalDeviceFeatures& Features();  // the ones enabled
VkResolveModeFlags DepthResolveModes();  // (the device's supported ones)
#endif
#ifdef AMD_VULKAN_MEMORY_ALLOCATOR_H
VmaAllocator Allocator();
#endif

// Every submit is numbered (a timeline semaphore): what is recorded now finishes
// with RecordingSerial(). Waiting for one not yet submitted submits it first
uint32_t MaxPushDescriptors();
uint64_t RecordingSerial();
uint64_t FenceSerial();  // what a GL fence made now waits for (the last submit, if nothing's recorded since)
uint64_t CompletedSerial();
bool WaitSerial(uint64_t serial, uint64_t timeout_ns = UINT64_MAX);
// Submits what's recorded so far and goes on in a new command buffer (big
// uploads, readbacks, GL fences waited on)
void Flush();
// Runs once the GPU has finished everything recorded so far
void DeferDestroy(std::function<void()> destroy);
// Runs before each submit (the resource layer closes its staging), and before the
// device goes (everything still alive is destroyed)
void OnSubmit(std::function<void()> before_submit);
void OnShutdown(std::function<void()> before_shutdown);
void OnFrameBegin(std::function<void()> frame_begin);  // (a new frame's commands open)
// Runs before the renderer gives up (the GPU lost, a swapchain or frame it can't make)
void OnFatal(std::function<void()> before_fatal);
void SetBeforeTransfer(std::function<void()> before_transfer);

}  // namespace VkBackend
