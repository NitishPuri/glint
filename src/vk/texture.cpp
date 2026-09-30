#include "vk/texture.h"

#include <algorithm>
#include <stdexcept>

#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/upload.h"

namespace glint::vk {

namespace {

void transitionMips(VkCommandBuffer cmd, VkImage image, uint32_t baseMip, uint32_t mipCount, VkImageLayout from,
                    VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1};
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

}  // namespace

Image createTexture(const Context& ctx, const ImageData& image, bool mipmaps, std::string_view name) {
  constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  const VkExtent2D extent{uint32_t(image.width), uint32_t(image.height)};
  uint32_t levels = 1;
  if (mipmaps) {
    for (uint32_t size = std::max(extent.width, extent.height); size > 1; size /= 2) ++levels;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      throw std::runtime_error("RGBA8 doesn't support linear blits");
    }
  }

  Image texture(ctx, {format, extent, levels,
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, name});
  Buffer staging(ctx, image.sizeBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  staging.write(image.pixels.data(), image.sizeBytes());

  OneTimeCommands once(ctx);
  const VkCommandBuffer cmd = once.cmd();
  const VkImage img = texture.handle();
  transitionMips(cmd, img, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {extent.width, extent.height, 1};
  vkCmdCopyBufferToImage(cmd, staging.handle(), img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  int32_t w = int32_t(extent.width), h = int32_t(extent.height);
  for (uint32_t i = 1; i < levels; ++i) {
    transitionMips(cmd, img, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
    blit.srcOffsets[1] = {w, h, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
    blit.dstOffsets[1] = {std::max(w / 2, 1), std::max(h / 2, 1), 1};
    vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
    w = std::max(w / 2, 1);
    h = std::max(h / 2, 1);
  }
  if (levels > 1) {
    transitionMips(cmd, img, 0, levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  transitionMips(cmd, img, levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  once.submitAndWait();
  return texture;
}

VkSampler createSampler(VkDevice device, const SamplerDesc& desc) {
  VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = desc.filter;
  info.minFilter = desc.filter;
  info.mipmapMode = desc.mipmaps ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = desc.address;
  info.addressModeV = desc.address;
  info.addressModeW = desc.address;
  info.maxLod = desc.mipmaps ? VK_LOD_CLAMP_NONE : 0.0f;
  info.compareEnable = desc.compare ? VK_TRUE : VK_FALSE;
  info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  info.borderColor = desc.border;
  VkSampler sampler = VK_NULL_HANDLE;
  VK_CHECK(vkCreateSampler(device, &info, nullptr, &sampler));
  if (!desc.name.empty()) debug::setName(device, VK_OBJECT_TYPE_SAMPLER, sampler, desc.name);
  return sampler;
}

}  // namespace glint::vk
