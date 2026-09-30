#pragma once

// vkCmdBeginRendering with the attachment setup 01–03 write out raw, plus the per-frame depth transition.

#include <glm/glm.hpp>

#include "vk/barrier.h"
#include "vk/vk.h"

namespace glint::vk {

struct RenderTarget {
  VkImageView color = VK_NULL_HANDLE;  // null: no color attachment (depth-only pass)
  glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};
  VkImageView depth = VK_NULL_HANDLE;  // null: no depth attachment
  bool storeDepth = false;             // true if a later pass reads the depth (shadow map, depth view)
};

// Color in COLOR_ATTACHMENT_OPTIMAL, depth in DEPTH_ATTACHMENT_OPTIMAL; both cleared (depth to 1).
inline void beginRendering(VkCommandBuffer cmd, VkExtent2D extent, const RenderTarget& target) {
  VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = target.color;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.clearValue.color = {{target.clearColor.r, target.clearColor.g, target.clearColor.b, target.clearColor.a}};
  VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = target.depth;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = target.storeDepth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = target.color ? 1 : 0;
  rendering.pColorAttachments = target.color ? &color : nullptr;
  rendering.pDepthAttachment = target.depth ? &depth : nullptr;
  vkCmdBeginRendering(cmd, &rendering);
}

// A depth buffer that is only used within the frame (cleared, tested, discarded): UNDEFINED -> attachment,
// ordered after the previous frame's depth writes (both frames in flight share it). As in 02_cube.
inline void transitionDepthForRendering(VkCommandBuffer cmd, VkImage depth) {
  constexpr VkPipelineStageFlags2 tests =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  transitionImage(cmd, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, tests, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  tests, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

}  // namespace glint::vk
