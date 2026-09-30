// 02_cube — Vulkan. New here, all written raw:
//   - vertex/index data in DEVICE_LOCAL memory, uploaded through a staging buffer + vkCmdCopyBuffer
//   - a uniform buffer per frame in flight, reached through a descriptor set
//   - a depth image we create, allocate, transition and attach ourselves
//   - a pipeline with depth test and (dynamic) back-face culling
// (vk/buffer.h, vk/mesh.h and vk/pipeline.h wrap exactly this code for 03 onwards.)

#include <array>
#include <cstring>
#include <stdexcept>

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/debug.h"
#include "vk/frame.h"
#include "vk/shader.h"
#include "vk/technique.h"

namespace glint::cube {

namespace {
// 32-bit float depth: supported as a depth attachment by every desktop GPU. (GL's default framebuffer
// depth was 24-bit, chosen by GLFW/the driver.)
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}  // namespace

class CubeVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    const MeshData cube = makeCube();
    m_indexCount = uint32_t(cube.indices.size());

    // --- geometry: staging buffer -> device-local buffers ---------------------------------------------
    uploadToDeviceLocal(cube.positions.data(), cube.positions.size() * sizeof(glm::vec3),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_positions, m_positionsMemory, "02 positions");
    uploadToDeviceLocal(cube.indices.data(), cube.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        m_indices, m_indicesMemory, "02 indices");

    // --- uniform buffers: one per frame in flight --------------------------------------------------------
    // GL overwrites one uniform whenever it likes; the driver keeps the GPU's in-flight copy intact.
    // In VK, frame N+1 is being recorded while the GPU may still read frame N's matrix, so each frame slot
    // gets its own buffer. Host-visible + coherent, mapped once and left mapped ("persistent mapping").
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      createBuffer(sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_ubo[i],
                   m_uboMemory[i]);
      VK_CHECK(vkMapMemory(device, m_uboMemory[i], 0, sizeof(glm::mat4), 0, &m_uboMapped[i]));
      vk::debug::setName(device, VK_OBJECT_TYPE_BUFFER, m_ubo[i], fmt::format("02 ubo {}", i));
    }

    // --- descriptors: how the shader's `set = 0, binding = 0` finds the uniform buffer ------------------
    // Three objects:
    //   layout: the *shape* of a set (binding 0 = one uniform buffer, visible to the vertex stage)
    //   pool:   memory to allocate sets from
    //   set:    an instance of the layout, pointing at a concrete buffer (one per frame in flight)
    // GL's equivalent is just "the uniform" (or glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo)).
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_setLayout));

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = vk::kFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    std::array<VkDescriptorSetLayout, vk::kFramesInFlight> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = vk::kFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()));

    // Point each set at its frame's buffer. Written once: the *contents* of the buffer change every frame,
    // the descriptor (which buffer) never does.
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      VkDescriptorBufferInfo bufferInfo{m_ubo[i], 0, sizeof(glm::mat4)};
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = m_sets[i];
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo = &bufferInfo;
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // --- pipeline --------------------------------------------------------------------------------------
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_setLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout));
    createPipeline(swapchainFormat);
  }

  ~CubeVK() override {
    const VkDevice device = m_ctx->device;
    destroyDepth();
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);  // frees its sets too
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      vkDestroyBuffer(device, m_ubo[i], nullptr);
      vkFreeMemory(device, m_uboMemory[i], nullptr);  // implicitly unmaps
    }
    vkDestroyBuffer(device, m_indices, nullptr);
    vkFreeMemory(device, m_indicesMemory, nullptr);
    vkDestroyBuffer(device, m_positions, nullptr);
    vkFreeMemory(device, m_positionsMemory, nullptr);
  }

  void update(float dt, Frame& frame) override {
    m_angle += m_params.rotationSpeed * dt;
    // The depth buffer must match the swapchain size. GL's default framebuffer resizes its own.
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depthExtent.width || size.height != m_depthExtent.height) {
      // The old depth image may still be used by a frame in flight: wait before destroying it. (Resizes
      // are rare; a fancier version would defer the destruction by kFramesInFlight frames.)
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      destroyDepth();
      createDepth(size);
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    // This frame's MVP into this frame slot's buffer. Safe: the app waited for this slot's fence, so the
    // GPU is done with the previous frame that used it. VK: clip-space z in [0, 1].
    const glm::mat4 mvp =
        info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne) * model(m_angle);
    std::memcpy(m_uboMapped[info.frameInFlight], &mvp, sizeof(mvp));  // coherent: no flush needed

    // The depth image: we don't need last frame's contents (we clear it), so UNDEFINED -> attachment.
    // src = last frame's depth writes: one depth image is shared by both frames in flight, and this barrier
    // makes this frame's depth test wait until the previous frame's is done with it.
    vk::transitionImage(cmd, m_depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = info.targetView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{m_params.clearColor.r, m_params.clearColor.g, m_params.clearColor.b, 1.0f}};
    // GL: glClearDepth(1.0) + GL_DEPTH_BUFFER_BIT. storeOp DONT_CARE: nobody reads depth after this pass,
    // so the GPU may skip writing it back to memory.
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, info.extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const VkViewport viewport{0.0f, float(info.extent.height), float(info.extent.width), -float(info.extent.height),
                              0.0f, 1.0f};  // negative height: GL's +Y up
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, info.extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    // Dynamic state (core in 1.3): culling can change per frame without a second pipeline.
    vkCmdSetCullMode(cmd, m_params.cullBackFaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);

    // GL: glUseProgram + glProgramUniform. VK: bind the set that points at this frame's uniform buffer.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                            &m_sets[info.frameInFlight], 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_positions, &offset);
    vkCmdBindIndexBuffer(cmd, m_indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params); }

 private:
  // vkCreateBuffer + memory of the requested kind + bind (01_triangle wrote the memory-type loop by hand).
  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags, VkBuffer& buffer,
                    VkDeviceMemory& memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VK_CHECK(vkCreateBuffer(m_ctx->device, &info, nullptr, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_ctx->device, buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = m_ctx->findMemoryType(req.memoryTypeBits, memoryFlags);
    VK_CHECK(vkAllocateMemory(m_ctx->device, &alloc, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(m_ctx->device, buffer, memory, 0));
  }

  // Staging upload. DEVICE_LOCAL memory is the fastest for the GPU to read but (on a discrete GPU) the CPU
  // can't map it. So: CPU writes a host-visible *staging* buffer, the GPU copies it across, staging is freed.
  // GL: glNamedBufferStorage(buf, size, data, 0) — the driver does this same dance internally.
  void uploadToDeviceLocal(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                           VkDeviceMemory& memory, const char* name) {
    const VkDevice device = m_ctx->device;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(device, stagingMemory, 0, size, 0, &mapped));
    std::memcpy(mapped, data, size);
    vkUnmapMemory(device, stagingMemory);

    // TRANSFER_DST: it's the target of the copy.
    createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);
    vk::debug::setName(device, VK_OBJECT_TYPE_BUFFER, buffer, name);

    // A one-off command buffer just for the copy, submitted and waited for right away. (Simple; a real
    // loader would batch uploads and wait on a fence instead of stalling the queue.)
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_ctx->queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    const VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging, buffer, 1, &region);
    // Make the copy's writes visible to the vertex/index fetch of every later draw. Without it, sync
    // validation reports a read-after-write hazard on the buffer.
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    VK_CHECK(vkQueueSubmit2(m_ctx->queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_ctx->queue));  // after this, the staging buffer is no longer in use

    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
  }

  void createDepth(VkExtent2D size) {
    const VkDevice device = m_ctx->device;
    m_depthExtent = size;
    // An image is a buffer with a format, dimensions, mip levels and a memory layout the driver may tile.
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kDepthFormat;
    info.extent = {size.width, size.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;  // GPU-friendly, opaque layout (vs LINEAR: row-major, CPU-readable)
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &info, nullptr, &m_depthImage));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, m_depthImage, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = m_ctx->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &m_depthMemory));
    VK_CHECK(vkBindImageMemory(device, m_depthImage, m_depthMemory, 0));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kDepthFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_depthView));
    vk::debug::setName(device, VK_OBJECT_TYPE_IMAGE, m_depthImage, "02 depth");
  }

  void destroyDepth() {
    const VkDevice device = m_ctx->device;
    vkDestroyImageView(device, m_depthView, nullptr);
    vkDestroyImage(device, m_depthImage, nullptr);
    vkFreeMemory(device, m_depthMemory, nullptr);
    m_depthView = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthMemory = VK_NULL_HANDLE;
    m_depthExtent = {0, 0};
  }

  // Same structure as 01's pipeline; the differences are depth, culling and the descriptor-set layout.
  void createPipeline(VkFormat colorFormat) {
    const VkDevice device = m_ctx->device;
    VkShaderModule vs = vk::loadShaderModule(device, "02_cube/cube.vert.spv");
    VkShaderModule fs = vk::loadShaderModule(device, "02_cube/cube.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                 "main"};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs,
                 "main"};

    const VkVertexInputBindingDescription binding{0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attribute{kAttribPosition, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                            VK_DYNAMIC_STATE_CULL_MODE};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 3;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;  // overridden by vkCmdSetCullMode (dynamic)
    // Same winding as GL (counter-clockwise = front). The negative-height viewport flips Y *and* the apparent
    // winding back, so GL-style data works unchanged.
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // GL: glEnable(GL_DEPTH_TEST) + glDepthFunc(GL_LESS) (+ glDepthMask(GL_TRUE)).
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = kDepthFormat;  // new vs 01: this pipeline renders with a depth attachment

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_pipelineLayout;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline));
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    vk::debug::setName(device, VK_OBJECT_TYPE_PIPELINE, m_pipeline, "02 cube pipeline");
  }

  Params m_params;
  float m_angle = 0.0f;
  vk::Context* m_ctx = nullptr;

  VkBuffer m_positions = VK_NULL_HANDLE, m_indices = VK_NULL_HANDLE;
  VkDeviceMemory m_positionsMemory = VK_NULL_HANDLE, m_indicesMemory = VK_NULL_HANDLE;
  uint32_t m_indexCount = 0;

  std::array<VkBuffer, vk::kFramesInFlight> m_ubo{};
  std::array<VkDeviceMemory, vk::kFramesInFlight> m_uboMemory{};
  std::array<void*, vk::kFramesInFlight> m_uboMapped{};

  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, vk::kFramesInFlight> m_sets{};
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;

  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
  VkImageView m_depthView = VK_NULL_HANDLE;
  VkExtent2D m_depthExtent{0, 0};
};

GLINT_REGISTER_VK("02_cube", CubeVK);

}  // namespace glint::cube
