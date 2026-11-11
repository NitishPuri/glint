#pragma once

// Move-only VkImage + VkDeviceMemory + one VkImageView, as 02_cube creates its depth buffer raw
// (vkCreateImage, vkGetImageMemoryRequirements, vkAllocateMemory, vkBindImageMemory, vkCreateImageView).
// Always DEVICE_LOCAL, optimal tiling, 2D (or 3D with depth > 1, as 16_noise creates one raw). Contents and layout
// are the caller's business.
// Memory: raw (its own VkDeviceMemory) or VMA (a range of a shared block, or a *dedicated* block when the driver
// prefers one — typical for render targets), chosen by the Context (vk/memory.h).

#include <string_view>
#include <utility>

#include "vk/context.h"
#include "vk/memory.h"

namespace glint::vk {

struct ImageDesc {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  uint32_t mipLevels = 1;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;  // DEPTH for depth formats
  std::string_view name;
  uint32_t depth = 1;  // > 1: a 3D image (VK_IMAGE_TYPE_3D, extent.depth = depth) with a 3D view
};

class Image {
 public:
  Image() = default;
  Image(const Context& ctx, const ImageDesc& desc);
  ~Image();
  Image(Image&& other) noexcept { swap(other); }
  Image& operator=(Image&& other) noexcept {
    swap(other);
    return *this;
  }
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  VkImage handle() const { return m_image; }
  VkImageView view() const { return m_view; }
  VkFormat format() const { return m_format; }
  VkExtent2D extent() const { return m_extent; }
  uint32_t mipLevels() const { return m_mipLevels; }
  explicit operator bool() const { return m_image != VK_NULL_HANDLE; }

 private:
  void swap(Image& other) noexcept {
    std::swap(m_device, other.m_device);
    std::swap(m_image, other.m_image);
    std::swap(m_memory, other.m_memory);
    std::swap(m_allocator, other.m_allocator);
    std::swap(m_allocation, other.m_allocation);
    std::swap(m_rawBytes, other.m_rawBytes);
    std::swap(m_view, other.m_view);
    std::swap(m_format, other.m_format);
    std::swap(m_extent, other.m_extent);
    std::swap(m_mipLevels, other.m_mipLevels);
  }

  VkDevice m_device = VK_NULL_HANDLE;
  VkImage m_image = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;    // raw path
  VmaAllocator m_allocator = VK_NULL_HANDLE;    // VMA path
  VmaAllocation m_allocation = VK_NULL_HANDLE;
  VkDeviceSize m_rawBytes = 0;
  VkImageView m_view = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  VkExtent2D m_extent{};
  uint32_t m_mipLevels = 1;
};

}  // namespace glint::vk
