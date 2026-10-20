//-----------------------------------------------------------------------------
//           Name: vk_mem_alloc.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The Vulkan Memory Allocator's implementation (VMA 3.3.0,
//                 MIT, Libraries/vma-3.3.0). Its Vulkan functions come from
//                 volk (vmaImportVulkanFunctionsFromVolk), never the loader's
//                 exports: vulkan-1.dll is loaded at run time
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
