#pragma once

// Image layout transitions with synchronization2 (vkCmdPipelineBarrier2).
//
// A VK image is always in some *layout* (how its memory is arranged for a use: attachment, sampling,
// transfer, presentation), and moving between uses needs a barrier that (1) makes the earlier writes
// visible to the later reads/writes and (2) changes the layout. GL does all of this invisibly.
//
// Each barrier names both sides: "after these stages finished these accesses (src) ... before these stages
// do these accesses (dst)". Written out at every call site on purpose: the stages/access masks *are* the
// lesson.

#include "vk/vk.h"

namespace glint::vk {

inline void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,  //
                            VkImageLayout oldLayout, VkImageLayout newLayout,               //
                            VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,       //
                            VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
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
  barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

}  // namespace glint::vk
