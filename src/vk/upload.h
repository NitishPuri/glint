#pragma once

// A throwaway command buffer for one-off GPU work at load time (staging copies, mip generation), submitted
// and waited for right away — what 02_cube's uploadToDeviceLocal does raw. Stalls the queue: fine at load
// time, not in the frame loop.
//
//   vk::OneTimeCommands once(ctx);
//   vkCmdCopyBuffer(once.cmd(), ...);
//   once.submitAndWait();

#include "vk/context.h"

namespace glint::vk {

class OneTimeCommands {
 public:
  explicit OneTimeCommands(const Context& ctx);
  ~OneTimeCommands();
  OneTimeCommands(const OneTimeCommands&) = delete;
  OneTimeCommands& operator=(const OneTimeCommands&) = delete;

  VkCommandBuffer cmd() const { return m_cmd; }
  // Ends the command buffer, submits it and blocks until the GPU is done (vkQueueWaitIdle).
  void submitAndWait();

 private:
  const Context& m_ctx;
  VkCommandPool m_pool = VK_NULL_HANDLE;
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  bool m_submitted = false;
};

}  // namespace glint::vk
