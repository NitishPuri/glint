#pragma once

// Image layout transitions with synchronization2 (vkCmdPipelineBarrier2).
//
// A VK image is always in some *layout* (how its memory is arranged for a use: attachment, sampling,
// transfer, presentation), and moving between uses needs a barrier that (1) makes the earlier writes
// visible to the later reads/writes and (2) changes the layout. GL does all of this invisibly.
//
// Convention: before a shader reads an image through a descriptor, dstAccess = VK_ACCESS_2_SHADER_READ_BIT
// (any shader read), not the narrower SHADER_SAMPLED_READ. The synchronization validation layer tracks
// descriptor image reads as generic shader reads and reports READ_AFTER_WRITE otherwise (07 hit it).
//
// Each barrier names both sides: "after these stages finished these accesses (src) ... before these stages
// do these accesses (dst)". Written out at every call site on purpose: the stages/access masks *are* the
// lesson.

#include "vk/vk.h"

namespace glint::vk {

inline void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,  //
                            VkImageLayout oldLayout, VkImageLayout newLayout,               //
                            VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,       //
                            VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,       //
                            uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS) {
  VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = oldLayout;  // UNDEFINED = "don't care about the old contents" (cheapest)
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  // Default: the whole image. A mip range lets levels of one image sit in different layouts (13_bloom
  // renders into level i while sampling level i-1). GL has no layouts, so nothing like this exists there.
  barrier.subresourceRange = {aspect, baseMip, mipCount, 0, VK_REMAINING_ARRAY_LAYERS};

  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

}  // namespace glint::vk
