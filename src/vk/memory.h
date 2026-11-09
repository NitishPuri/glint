#pragma once

// Device memory accounting + the Vulkan Memory Allocator (VMA), introduced after 01–17 allocated everything raw.
//
// Raw (--no-vma): every vk::Buffer / vk::Image is one vkCreate* + vkAllocateMemory of exactly its size, its own
// VkDeviceMemory object. Simple and visible — and what VMA's docs call the thing not to do at scale: drivers allow
// a limited number of allocations (maxMemoryAllocationCount, often 4096), each one is slow, and small ones waste
// alignment and page granularity.
// VMA: allocates large VkDeviceMemory *blocks* and sub-allocates buffers/images inside them (offset + size),
// picks the memory type from what you intend (device-local? CPU writes? CPU reads?), gives render targets and
// other big images their own *dedicated* allocation when the driver prefers it, and keeps mapped pointers.
// GL: all of this is the driver's job; glNamedBufferStorage flags are the only hint you give.

#include <string>

#include "vk/vk.h"

// VMA's handles, forward-declared exactly as vk_mem_alloc.h declares them (VK_DEFINE_HANDLE), so headers don't
// need the 20k-line header. Only .cpp files that call VMA include <vk_mem_alloc.h>.
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace glint::vk {

class Context;

// Raw path bookkeeping: live vk::Buffer/vk::Image allocations made with vkAllocateMemory (one each).
void trackRawAllocation(VkDeviceSize bytes, int delta);

// "VMA: 142 allocations in 9 VkDeviceMemory blocks, 98.3 MB used of 112.0 MB allocated" or
// "raw: 142 allocations = 142 VkDeviceMemory objects, 98.3 MB". Helper-made allocations only (techniques that
// allocate raw on purpose — 01, 02, 16 — aren't counted).
std::string memoryReport(const Context& ctx);

// VMA's JSON statistics (every block, every sub-allocation with its offset/size/type), for tools/diagrams.py.
// No-op without VMA.
void writeVmaStats(const Context& ctx, const std::string& path);

}  // namespace glint::vk
