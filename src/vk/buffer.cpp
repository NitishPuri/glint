#include "vk/buffer.h"

#include <vk_mem_alloc.h>

#include <cstring>

#include "vk/debug.h"
#include "vk/upload.h"

namespace glint::vk {

Buffer::Buffer(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory,
               std::string_view name)
    : m_device(ctx.device), m_size(size) {
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;

  if (ctx.allocator) {
    // VMA: say what the memory is for, not which type index to use. requiredFlags keeps the caller's exact
    // property flags (e.g. HOST_CACHED for 15's readback); the HOST_ACCESS flag tells VMA the CPU will touch
    // it (required to map with VMA_MEMORY_USAGE_AUTO) and whether it reads (CACHED requested) or just writes.
    // MAPPED_BIT: mapped once at creation for the allocation's whole life, like our raw mapped() does lazily.
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    alloc.requiredFlags = memory;
    if (memory & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | (memory & VK_MEMORY_PROPERTY_HOST_CACHED_BIT
                                                            ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                                            : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    }
    VmaAllocationInfo result{};
    VK_CHECK(vmaCreateBuffer(ctx.allocator, &info, &alloc, &m_buffer, &m_allocation, &result));
    m_allocator = ctx.allocator;
    m_mapped = result.pMappedData;  // null unless host-visible
    if (!name.empty()) {
      debug::setName(m_device, VK_OBJECT_TYPE_BUFFER, m_buffer, name);
      vmaSetAllocationName(m_allocator, m_allocation, std::string(name).c_str());  // shows up in --vma-stats
    }
    return;
  }

  VK_CHECK(vkCreateBuffer(m_device, &info, nullptr, &m_buffer));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(m_device, m_buffer, &req);
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits, memory);
  VK_CHECK(vkAllocateMemory(m_device, &alloc, nullptr, &m_memory));
  VK_CHECK(vkBindBufferMemory(m_device, m_buffer, m_memory, 0));
  trackRawAllocation(req.size, +1);
  m_rawBytes = req.size;
  if (!name.empty()) debug::setName(m_device, VK_OBJECT_TYPE_BUFFER, m_buffer, name);
}

Buffer::~Buffer() {
  if (!m_device) return;
  if (m_allocator) {
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);  // buffer + its range of the block (unmaps)
    return;
  }
  vkDestroyBuffer(m_device, m_buffer, nullptr);
  vkFreeMemory(m_device, m_memory, nullptr);  // implicitly unmaps
  trackRawAllocation(m_rawBytes, -1);
}

void* Buffer::mapped() {
  // VMA path: mapped at creation (or not host-visible: then mapping is a bug either way).
  if (!m_mapped && !m_allocator) VK_CHECK(vkMapMemory(m_device, m_memory, 0, VK_WHOLE_SIZE, 0, &m_mapped));
  return m_mapped;
}

void Buffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset) {
  std::memcpy(static_cast<char*>(mapped()) + offset, data, size);
}

Buffer createDeviceLocalBuffer(const Context& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                               std::string_view name) {
  Buffer staging(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  staging.write(data, size);
  Buffer buffer(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, name);

  OneTimeCommands once(ctx);
  const VkBufferCopy region{0, 0, size};
  vkCmdCopyBuffer(once.cmd(), staging.handle(), buffer.handle(), 1, &region);
  // The buffer may be read as vertices, indices, uniforms, ... — so make the copy visible to all reads.
  VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer.handle();
  barrier.size = VK_WHOLE_SIZE;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.bufferMemoryBarrierCount = 1;
  dependency.pBufferMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(once.cmd(), &dependency);
  once.submitAndWait();  // staging must outlive the copy
  return buffer;
}

}  // namespace glint::vk
