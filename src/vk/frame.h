#pragma once

// Frames in flight: the CPU records frame N+1 while the GPU still renders frame N. Everything the CPU
// writes per frame (command buffer, and later per-frame uniform buffers) exists once per frame in flight,
// and a fence tells us when the GPU is done with a slot so we can reuse it.
// GL does the same inside the driver; you only see it as glfwSwapBuffers sometimes blocking.

#include <array>

#include "vk/context.h"

namespace glint::vk {

inline constexpr uint32_t kFramesInFlight = 2;

struct FrameSync {
  VkCommandPool commandPool = VK_NULL_HANDLE;  // reset as a whole each time the slot is reused
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkFence inFlight = VK_NULL_HANDLE;          // signalled when the GPU finished this slot's submission
  VkSemaphore imageAcquired = VK_NULL_HANDLE;  // signalled when the acquired swapchain image is ready
};

class Frames {
 public:
  explicit Frames(const Context& ctx);
  ~Frames();
  Frames(const Frames&) = delete;
  Frames& operator=(const Frames&) = delete;

  FrameSync& operator[](uint32_t i) { return m_frames[i]; }

 private:
  const Context& m_ctx;
  std::array<FrameSync, kFramesInFlight> m_frames;
};

}  // namespace glint::vk
