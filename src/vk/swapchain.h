#pragma once

// The images we render into and hand to the window system. GL's default framebuffer, made explicit:
// you pick the format, the number of images, the present mode (vsync) and the size, and you must
// recreate it whenever the window size changes (VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR).

#include <vector>

#include "vk/context.h"

namespace glint::vk {

class Swapchain {
 public:
  Swapchain(const Context& ctx, VkExtent2D size, bool vsync);
  ~Swapchain();
  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;

  // Builds a new swapchain for the new size (passing the old one as oldSwapchain) and destroys the old one.
  // The caller must make sure the GPU no longer uses the old images (vkDeviceWaitIdle).
  void recreate(VkExtent2D size);

  VkSwapchainKHR handle = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  uint32_t minImageCount = 0;
  std::vector<VkImage> images;          // owned by the swapchain
  std::vector<VkImageView> views;       // ours
  // Signalled when rendering to image i is done; present waits on it. One per *image*, not per frame in
  // flight: the presentation engine may still hold the semaphore of image i until i is acquired again.
  std::vector<VkSemaphore> renderDone;

 private:
  void create(VkExtent2D size, VkSwapchainKHR old);
  void destroyViews();

  const Context& m_ctx;
  bool m_vsync;
};

}  // namespace glint::vk
