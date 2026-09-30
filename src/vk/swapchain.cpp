#include "vk/swapchain.h"

#include <algorithm>
#include <string>

#include "core/log.h"
#include "vk/debug.h"

namespace glint::vk {

Swapchain::Swapchain(const Context& ctx, VkExtent2D size, bool vsync) : m_ctx(ctx), m_vsync(vsync) {
  create(size, VK_NULL_HANDLE);
}

Swapchain::~Swapchain() {
  destroyViews();
  vkDestroySwapchainKHR(m_ctx.device, handle, nullptr);
}

void Swapchain::recreate(VkExtent2D size) {
  VkSwapchainKHR old = handle;
  destroyViews();
  create(size, old);
  vkDestroySwapchainKHR(m_ctx.device, old, nullptr);
}

void Swapchain::destroyViews() {
  for (VkImageView view : views) vkDestroyImageView(m_ctx.device, view, nullptr);
  for (VkSemaphore semaphore : renderDone) vkDestroySemaphore(m_ctx.device, semaphore, nullptr);
  views.clear();
  renderDone.clear();
}

void Swapchain::create(VkExtent2D size, VkSwapchainKHR old) {
  const VkPhysicalDevice gpu = m_ctx.physicalDevice;
  VkSurfaceCapabilitiesKHR caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, m_ctx.surface, &caps));

  // --- format ---------------------------------------------------------------------------------------------
  // B8G8R8A8_UNORM, the same thing GL's default framebuffer is here: shader output goes to the screen as
  // is (no linear->sRGB conversion). An *_SRGB format would encode on write — correct gamma, but then GL
  // and VK output would differ. Gamma is a technique of its own for later.
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, m_ctx.surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, m_ctx.surface, &formatCount, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (const VkSurfaceFormatKHR& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;
  }
  format = chosen.format;

  // --- present mode (= vsync) -------------------------------------------------------------------------------
  // FIFO: wait for vblank, always supported (GL: glfwSwapInterval(1)). Without vsync prefer MAILBOX (newest
  // frame wins, no tearing), else IMMEDIATE (may tear; GL: glfwSwapInterval(0)).
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  if (!m_vsync) {
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, m_ctx.surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, m_ctx.surface, &modeCount, modes.data());
    for (VkPresentModeKHR mode : modes) {
      if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR && presentMode == VK_PRESENT_MODE_FIFO_KHR) presentMode = mode;
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = mode;
    }
  }

  // --- size and image count ---------------------------------------------------------------------------------
  // currentExtent is the window size, unless the platform lets us choose (0xFFFFFFFF, e.g. Wayland).
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
  } else {
    extent.width = std::clamp(size.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(size.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  // One more than the minimum, so we never wait on the driver to release an image to render into.
  minImageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) minImageCount = std::min(minImageCount, caps.maxImageCount);

  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = m_ctx.surface;
  info.minImageCount = minImageCount;
  info.imageFormat = chosen.format;
  info.imageColorSpace = chosen.colorSpace;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  // COLOR_ATTACHMENT: we render into it. TRANSFER_SRC: --screenshot copies it to a buffer.
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;  // one queue family uses it
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = presentMode;
  info.clipped = VK_TRUE;
  info.oldSwapchain = old;  // lets the driver reuse resources and keep presenting while we switch
  VK_CHECK(vkCreateSwapchainKHR(m_ctx.device, &info, nullptr, &handle));

  uint32_t imageCount = 0;
  vkGetSwapchainImagesKHR(m_ctx.device, handle, &imageCount, nullptr);
  images.resize(imageCount);
  vkGetSwapchainImagesKHR(m_ctx.device, handle, &imageCount, images.data());

  // An image view says how to interpret an image (format, which mips/layers). Attachments and samplers
  // always go through views. (GL has no separate concept; the texture is its own view.)
  views.resize(imageCount);
  renderDone.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = images[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &views[i]));

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(m_ctx.device, &semaphoreInfo, nullptr, &renderDone[i]));

    const std::string n = std::to_string(i);
    debug::setName(m_ctx.device, VK_OBJECT_TYPE_IMAGE, images[i], "swapchain image " + n);
    debug::setName(m_ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW, views[i], "swapchain view " + n);
    debug::setName(m_ctx.device, VK_OBJECT_TYPE_SEMAPHORE, renderDone[i], "render done " + n);
  }

  log::info("swapchain  {}x{} {} {}, {} images", extent.width, extent.height, string_VkFormat(format),
            string_VkPresentModeKHR(presentMode), imageCount);
}

}  // namespace glint::vk
