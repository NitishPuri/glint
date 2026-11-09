#pragma once

// Move-only VkBuffer + its memory, as 01/02 create them raw (vkCreateBuffer,
// vkGetBufferMemoryRequirements, findMemoryType, vkAllocateMemory, vkBindBufferMemory).
// Two ways to get its memory, chosen by the Context (vk/memory.h):
//   raw  (--no-vma): its own VkDeviceMemory of exactly its size — what 01/02 do by hand
//   VMA  (default):  a range inside a shared block, memory type chosen from `memory` + intended host access

#include <string_view>
#include <utility>

#include "vk/context.h"
#include "vk/memory.h"

namespace glint::vk {

class Buffer {
 public:
  Buffer() = default;
  Buffer(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory,
         std::string_view name = {});
  ~Buffer();
  Buffer(Buffer&& other) noexcept { swap(other); }
  Buffer& operator=(Buffer&& other) noexcept {
    swap(other);
    return *this;
  }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  VkBuffer handle() const { return m_buffer; }
  VkDeviceSize size() const { return m_size; }

  // Host-visible buffers only: persistently mapped on first use, unmapped when the buffer is destroyed.
  void* mapped();
  // memcpy into the mapped buffer (host-coherent memory assumed: no flush).
  void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
  template <typename T>
  void write(const T& value) {
    write(&value, sizeof(T));
  }

 private:
  void swap(Buffer& other) noexcept {
    std::swap(m_device, other.m_device);
    std::swap(m_buffer, other.m_buffer);
    std::swap(m_memory, other.m_memory);
    std::swap(m_allocator, other.m_allocator);
    std::swap(m_allocation, other.m_allocation);
    std::swap(m_mapped, other.m_mapped);
    std::swap(m_size, other.m_size);
    std::swap(m_rawBytes, other.m_rawBytes);
  }

  VkDevice m_device = VK_NULL_HANDLE;
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;  // raw path
  VmaAllocator m_allocator = VK_NULL_HANDLE;  // VMA path
  VmaAllocation m_allocation = VK_NULL_HANDLE;
  void* m_mapped = nullptr;
  VkDeviceSize m_size = 0;
  VkDeviceSize m_rawBytes = 0;  // raw path: the allocation's size, for the memory report
};

// DEVICE_LOCAL buffer filled through a staging buffer + vkCmdCopyBuffer (02_cube's uploadToDeviceLocal).
// Blocks until the copy is done; the barrier after the copy makes the data visible to any later read.
Buffer createDeviceLocalBuffer(const Context& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                               std::string_view name = {});

}  // namespace glint::vk
