#include "vk/image.h"

#include "vk/debug.h"

namespace glint::vk {

Image::Image(const Context& ctx, const ImageDesc& desc)
    : m_device(ctx.device), m_format(desc.format), m_extent(desc.extent), m_mipLevels(desc.mipLevels) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = desc.format;
  info.extent = {desc.extent.width, desc.extent.height, 1};
  info.mipLevels = desc.mipLevels;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = desc.usage;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(m_device, &info, nullptr, &m_image));

  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(m_device, m_image, &req);
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkAllocateMemory(m_device, &alloc, nullptr, &m_memory));
  VK_CHECK(vkBindImageMemory(m_device, m_image, m_memory, 0));

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = m_image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = desc.format;
  viewInfo.subresourceRange = {desc.aspect, 0, desc.mipLevels, 0, 1};
  VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_view));

  if (!desc.name.empty()) {
    debug::setName(m_device, VK_OBJECT_TYPE_IMAGE, m_image, desc.name);
    debug::setName(m_device, VK_OBJECT_TYPE_IMAGE_VIEW, m_view, desc.name);
  }
}

Image::~Image() {
  if (!m_device) return;
  vkDestroyImageView(m_device, m_view, nullptr);
  vkDestroyImage(m_device, m_image, nullptr);
  vkFreeMemory(m_device, m_memory, nullptr);
}

}  // namespace glint::vk
