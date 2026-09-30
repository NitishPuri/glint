#include "vk/frame.h"

#include <string>

#include "vk/debug.h"

namespace glint::vk {

Frames::Frames(const Context& ctx) : m_ctx(ctx) {
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    FrameSync& f = m_frames[i];
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;  // buffers are re-recorded every frame
    poolInfo.queueFamilyIndex = ctx.queueFamily;
    VK_CHECK(vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &f.commandPool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = f.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx.device, &allocInfo, &f.commandBuffer));

    // Created signalled, so the very first wait on it returns immediately.
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(ctx.device, &fenceInfo, nullptr, &f.inFlight));

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr, &f.imageAcquired));

    const std::string n = std::to_string(i);
    debug::setName(ctx.device, VK_OBJECT_TYPE_COMMAND_BUFFER, f.commandBuffer, "frame " + n + " commands");
    debug::setName(ctx.device, VK_OBJECT_TYPE_FENCE, f.inFlight, "frame " + n + " in flight");
    debug::setName(ctx.device, VK_OBJECT_TYPE_SEMAPHORE, f.imageAcquired, "frame " + n + " image acquired");
  }
}

Frames::~Frames() {
  for (FrameSync& f : m_frames) {
    vkDestroySemaphore(m_ctx.device, f.imageAcquired, nullptr);
    vkDestroyFence(m_ctx.device, f.inFlight, nullptr);
    vkDestroyCommandPool(m_ctx.device, f.commandPool, nullptr);  // frees its command buffers too
  }
}

}  // namespace glint::vk
