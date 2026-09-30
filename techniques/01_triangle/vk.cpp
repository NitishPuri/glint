// 01_triangle — Vulkan, written raw: every VK object is created and used right here, no helpers.
// Read it next to gl.cpp: same vertex data (params.h), same picture, roughly 2.5x the code — and every
// extra line is something the GL driver decides for you.
//
// Objects: 2 shader modules, 1 pipeline layout (push constant range), 1 graphics pipeline,
//          2 buffers + 2 device memory allocations (vertices, indices).

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/assets.h"
#include "core/log.h"
#include "params.h"
#include "vk/debug.h"
#include "vk/technique.h"

namespace glint::triangle {

namespace {

// SPIR-V produced by glslc at build time (cmake/shaders.cmake). GL compiles GLSL text at runtime instead.
VkShaderModule loadShaderModule(VkDevice device, const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error(fmt::format("cannot open {} (built by glslc?)", path));
  std::vector<char> code(size_t(file.tellg()));
  file.seekg(0);
  file.read(code.data(), std::streamsize(code.size()));

  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t*>(code.data());  // std::vector's allocator aligns enough
  VkShaderModule module = VK_NULL_HANDLE;
  VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
  return module;
}

}  // namespace

class TriangleVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_device = ctx.device;

    // --- buffers: create, find memory, allocate, bind, fill --------------------------------------------------
    // GL: glCreateBuffers + glNamedBufferStorage and the driver picks the memory. VK: you do.
    createBuffer(ctx, sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vertexBuffer, m_vertexMemory);
    createBuffer(ctx, sizeof(kTriangleIndices) + sizeof(kQuadIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 m_indexBuffer, m_indexMemory);
    upload(m_vertexMemory, 0, kVertices, sizeof(kVertices));
    upload(m_indexMemory, 0, kTriangleIndices, sizeof(kTriangleIndices));
    upload(m_indexMemory, sizeof(kTriangleIndices), kQuadIndices, sizeof(kQuadIndices));

    // --- pipeline layout: what the shaders get from outside besides vertex attributes ------------------------
    // Here just one push constant range (the 4x4 matrix). Descriptor set layouts (uniform buffers, textures)
    // would go here too; 01 has none.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout));

    // --- graphics pipeline: shaders + *all* fixed-function state, baked into one immutable object --------------
    // GL: a program is only the shaders; vertex layout (VAO), depth/cull/blend (glEnable...) are separate
    // global state you can change between any two draws. VK: all of it is decided here, up front, which is
    // why pipelines are expensive to create and cheap to bind.
    const std::string dir = std::string(GLINT_SHADER_DIR) + "/01_triangle/";
    VkShaderModule vs = loadShaderModule(m_device, dir + "triangle.vert.spv");
    VkShaderModule fs = loadShaderModule(m_device, dir + "triangle.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // Vertex input: the same binding/attribute split as the GL DSA calls in gl.cpp
    // (glVertexArrayVertexBuffer ~ binding, glVertexArrayAttribFormat + AttribBinding ~ attribute).
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0] = {kAttribPosition, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)};
    attributes[1] = {kAttribColor, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // GL: the mode argument of each draw call

    // Viewport and scissor are *dynamic* (set while recording), so a window resize doesn't need a new
    // pipeline. Only their count is fixed here.
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;  // GL: glDisable(GL_CULL_FACE)
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth attachment at all (GL: glDisable(GL_DEPTH_TEST)).
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    // Dynamic rendering: instead of a VkRenderPass, the pipeline just states the attachment formats it
    // will render to. They must match the image views passed to vkCmdBeginRendering.
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = m_pipelineLayout;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

    // The pipeline has its own copy of the code; the modules can go.
    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, fs, nullptr);

    vk::debug::setName(m_device, VK_OBJECT_TYPE_PIPELINE, m_pipeline, "01 triangle pipeline");
    vk::debug::setName(m_device, VK_OBJECT_TYPE_BUFFER, m_vertexBuffer, "01 vertices");
    vk::debug::setName(m_device, VK_OBJECT_TYPE_BUFFER, m_indexBuffer, "01 indices");
  }

  ~TriangleVK() override {
    // The app waited for the GPU to go idle first — destroying something a queued frame still uses is
    // undefined behaviour in VK (GL just defers the free). vkDestroy*(VK_NULL_HANDLE) is a no-op.
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    vkFreeMemory(m_device, m_indexMemory, nullptr);
    vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    vkFreeMemory(m_device, m_vertexMemory, nullptr);
  }

  void update(float dt, Frame& /*frame*/) override { m_angle += m_params.rotationSpeed * dt; }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    // Begin rendering into the swapchain image, clearing it. GL: glBindFramebuffer(0) + glClear.
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = info.targetView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{m_params.clearColor.r, m_params.clearColor.g, m_params.clearColor.b, 1.0f}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, info.extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // VK's framebuffer +Y points *down*, GL's up. A negative-height viewport (origin at the bottom edge)
    // flips it back, so the same vertices and matrices give the same image as GL.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = float(info.extent.height);
    viewport.width = float(info.extent.width);
    viewport.height = -float(info.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, info.extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const glm::mat4 xform = transform(m_angle, info.frame.aspect());
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(xform), &xform);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    if (m_params.quad) {
      vkCmdDrawIndexed(cmd, 6, 1, /*firstIndex*/ uint32_t(std::size(kTriangleIndices)), 0, 0);
    } else {
      vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
    }

    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createBuffer(const vk::Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                    VkDeviceMemory& memory) {
    // 1. The buffer object: size and what it will be used for. No memory yet.
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_device, &info, nullptr, &buffer));

    // 2. Ask what memory it needs (size incl. padding, alignment, which memory types are allowed)...
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buffer, &req);

    // 3. ...and pick a memory type that is allowed *and* CPU-writable. HOST_VISIBLE: we can map it.
    // HOST_COHERENT: our writes become visible to the GPU without explicit flushes. On this APU that's plain
    // system RAM the GPU reads directly; on a discrete GPU it's slower for the GPU to read than VRAM —
    // which is why 02 introduces staging copies into DEVICE_LOCAL memory.
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < ctx.memoryProperties.memoryTypeCount; ++i) {
      if ((req.memoryTypeBits & (1u << i)) && (ctx.memoryProperties.memoryTypes[i].propertyFlags & wanted) == wanted) {
        typeIndex = i;
        break;
      }
    }
    if (typeIndex == UINT32_MAX) throw std::runtime_error("no host-visible coherent memory type");

    // 4. Allocate and bind. (One allocation per buffer is fine here; real apps sub-allocate big blocks —
    // there's a limit of maxMemoryAllocationCount, often 4096. VMA comes in a later phase.)
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = typeIndex;
    VK_CHECK(vkAllocateMemory(m_device, &alloc, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(m_device, buffer, memory, 0));
  }

  // Map, copy, unmap. The data is on the GPU's side the moment memcpy returns (coherent memory).
  void upload(VkDeviceMemory memory, VkDeviceSize offset, const void* data, size_t size) {
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(m_device, memory, offset, size, 0, &mapped));
    std::memcpy(mapped, data, size);
    vkUnmapMemory(m_device, memory);
  }

  Params m_params;
  float m_angle = 0.0f;
  VkDevice m_device = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
  VkBuffer m_indexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("01_triangle", TriangleVK);

}  // namespace glint::triangle
