#pragma once

// Graphics pipeline creation, as 01_triangle and 02_cube write it out raw. The desc lists only what
// differs between our techniques; everything else is fixed and documented in pipeline.cpp:
//   fill, counter-clockwise front faces (with the flipped viewport, like GL),
//   1 sample, dynamic viewport + scissor + cull mode (+ depth bias if requested).
//
// It's a plain struct, not a builder: read the fields, read createGraphicsPipeline(), done.

#include <string>
#include <vector>

#include "vk/mesh.h"
#include "vk/vk.h"

namespace glint::vk {

struct GraphicsPipelineDesc {
  std::string vertexShader;    // relative to GLINT_SHADER_DIR, e.g. "02_cube/cube.vert.spv"
  std::string fragmentShader;  // empty: no fragment stage (depth-only pass)
  VertexInput vertexInput;     // empty: attribute-less draws (full-screen triangle from gl_VertexIndex)
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // GL: the mode argument of each draw
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkFormat colorFormat = VK_FORMAT_UNDEFINED;  // UNDEFINED: no color attachment
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;  // UNDEFINED: no depth attachment
  bool depthTest = true;
  bool depthWrite = true;
  VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
  bool alphaBlend = false;     // src * a + dst * (1 - a)
  bool additiveBlend = false;  // src + dst (13_bloom's upsample accumulation)
  bool depthBias = false;   // enable depth bias; values set per frame with vkCmdSetDepthBias
  // Values for the fragment shader's `layout(constant_id = N) const ...`, fixed at pipeline creation
  // (10_specialization_constants). GL's equivalent: #defines injected before compiling.
  const VkSpecializationInfo* fragmentSpecialization = nullptr;
  std::string name;
};

VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc);

// Viewport covering `extent` with negative height (origin at the bottom edge), so +Y is up like GL; plus a
// matching scissor. (01/02 spell this out.)
void setFlippedViewport(VkCommandBuffer cmd, VkExtent2D extent);

}  // namespace glint::vk
