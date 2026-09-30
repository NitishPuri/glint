#include "vk/upload.h"

namespace glint::vk {

OneTimeCommands::OneTimeCommands(const Context& ctx) : m_ctx(ctx) {
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = ctx.queueFamily;
  VK_CHECK(vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &m_pool));
  VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = m_pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  VK_CHECK(vkAllocateCommandBuffers(ctx.device, &allocInfo, &m_cmd));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(m_cmd, &begin));
}

OneTimeCommands::~OneTimeCommands() {
  if (!m_submitted) submitAndWait();
  vkDestroyCommandPool(m_ctx.device, m_pool, nullptr);
}

void OneTimeCommands::submitAndWait() {
  VK_CHECK(vkEndCommandBuffer(m_cmd));
  VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmdInfo.commandBuffer = m_cmd;
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cmdInfo;
  VK_CHECK(vkQueueSubmit2(m_ctx.queue, 1, &submit, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(m_ctx.queue));
  m_submitted = true;
}

}  // namespace glint::vk
