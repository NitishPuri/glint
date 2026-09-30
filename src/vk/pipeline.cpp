#include "vk/pipeline.h"

#include "vk/debug.h"
#include "vk/shader.h"

namespace glint::vk {

VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  uint32_t stageCount = 0;
  stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                          loadShaderModule(device, desc.vertexShader), "main"};
  if (!desc.fragmentShader.empty()) {
    stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                            VK_SHADER_STAGE_FRAGMENT_BIT, loadShaderModule(device, desc.fragmentShader), "main"};
  }

  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = uint32_t(desc.vertexInput.bindings.size());
  vertexInput.pVertexBindingDescriptions = desc.vertexInput.bindings.data();
  vertexInput.vertexAttributeDescriptionCount = uint32_t(desc.vertexInput.attributes.size());
  vertexInput.pVertexAttributeDescriptions = desc.vertexInput.attributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                               VK_DYNAMIC_STATE_CULL_MODE};
  if (desc.depthBias) dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = uint32_t(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_BACK_BIT;  // dynamic: vkCmdSetCullMode must be called before drawing
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.depthBiasEnable = desc.depthBias ? VK_TRUE : VK_FALSE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  const bool hasDepth = desc.depthFormat != VK_FORMAT_UNDEFINED;
  depth.depthTestEnable = hasDepth && desc.depthTest ? VK_TRUE : VK_FALSE;
  depth.depthWriteEnable = hasDepth && desc.depthWrite ? VK_TRUE : VK_FALSE;
  depth.depthCompareOp = desc.depthCompare;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT;
  if (desc.alphaBlend) {  // GL: glEnable(GL_BLEND) + glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  const bool hasColor = desc.colorFormat != VK_FORMAT_UNDEFINED;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = hasColor ? 1 : 0;
  blend.pAttachments = &blendAttachment;

  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = hasColor ? 1 : 0;
  rendering.pColorAttachmentFormats = &desc.colorFormat;
  rendering.depthAttachmentFormat = desc.depthFormat;

  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = stageCount;
  info.pStages = stages;
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &inputAssembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = desc.layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));

  for (uint32_t i = 0; i < stageCount; ++i) vkDestroyShaderModule(device, stages[i].module, nullptr);
  if (!desc.name.empty()) debug::setName(device, VK_OBJECT_TYPE_PIPELINE, pipeline, desc.name);
  return pipeline;
}

void setFlippedViewport(VkCommandBuffer cmd, VkExtent2D extent) {
  const VkViewport viewport{0.0f, float(extent.height), float(extent.width), -float(extent.height), 0.0f, 1.0f};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &scissor);
}

}  // namespace glint::vk
