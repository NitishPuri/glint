// 03_textured_cube — Vulkan. Textures written raw (vk/texture.h wraps this code for 04 onwards):
//   - staging buffer -> image copy, with explicit layout transitions per mip level
//   - the mip chain generated with vkCmdBlitImage, level by level (GL: one glGenerateTextureMipmap call)
//   - immutable VkSamplers, one per filter mode
//   - a combined-image-sampler descriptor next to the uniform buffer
// Buffers, the depth image, the mesh and the pipeline use the vk/ helpers extracted after 02.

#include <array>

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/mesh.h"
#include "vk/pipeline.h"
#include "vk/technique.h"
#include "vk/upload.h"

namespace glint::textured_cube {

namespace {

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
// Same as GL's GL_RGBA8: UNORM, no sRGB decode (see the swapchain format note in vk/swapchain.cpp).
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Layout transition of mip levels [baseMip, baseMip + mipCount), written out here because per-mip
// transitions are the heart of mip generation. (vk::transitionImage always covers all mips.)
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

class TexturedCubeVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    m_mesh = vk::Mesh(ctx, makeCube(), "cube");
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("03 ubo {}", i));
    }
    loadTexture();
    createSamplers();

    // --- descriptors: binding 0 = uniform buffer (vertex), binding 1 = combined image sampler (fragment) ----
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_setLayout));

    const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = vk::kFramesInFlight;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    std::array<VkDescriptorSetLayout, vk::kFramesInFlight> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = vk::kFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()));
    // Binding 1 is written every frame (texture and sampler can change), binding 0 only here.
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      const VkDescriptorBufferInfo bufferInfo{m_ubo[i].handle(), 0, sizeof(glm::mat4)};
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = m_sets[i];
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo = &bufferInfo;
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_setLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout));

    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "03_textured_cube/textured.vert.spv";
    desc.fragmentShader = "03_textured_cube/textured.frag.spv";
    // Only what the shader reads: declaring unused attributes costs vertex fetches (validation warns).
    desc.vertexInput = m_mesh.vertexInput({kAttribPosition, kAttribUv});
    desc.layout = m_pipelineLayout;
    desc.colorFormat = swapchainFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = "03 textured pipeline";
    m_pipeline = vk::createGraphicsPipeline(device, desc);
  }

  ~TexturedCubeVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    for (VkSampler sampler : m_samplers) vkDestroySampler(device, sampler, nullptr);
    // vk::Image / vk::Buffer / vk::Mesh members clean up after themselves.
  }

  void update(float dt, Frame& frame) override {
    m_angle += m_params.rotationSpeed * dt;
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));  // the old depth image may be in use by a frame in flight
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "03 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const uint32_t f = info.frameInFlight;
    m_ubo[f].write(info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne) * model(m_angle));

    // Point binding 1 at the current texture + the sampler for the current filter mode. Updating a set is
    // only legal while no pending command buffer uses it: this frame slot's fence was waited on, so it's free.
    // GL: glBindTextureUnit + glBindSampler, any time.
    const VkDescriptorImageInfo imageInfo{m_samplers[size_t(m_params.filter)], m_texture.view(),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_sets[f];
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_ctx->device, 1, &write, 0, nullptr);

    vk::transitionImage(cmd, m_depth.handle(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
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
    color.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_depth.view();
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
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_sets[f], 0, nullptr);
    m_mesh.draw(cmd);
    vkCmdEndRendering(cmd);
  }

  void ui() override {
    if (paramsUi(m_params)) {
      // Both frames in flight may still sample the old image: wait before replacing it.
      // (GL: glDeleteTextures is always safe; the driver defers the free.)
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      loadTexture();
    }
  }

 private:
  void loadTexture() {
    const ImageData image = loadImage(assetPath(kTextures[m_params.texture]));
    const VkExtent2D extent{uint32_t(image.width), uint32_t(image.height)};
    uint32_t levels = 1;
    for (uint32_t size = std::max(extent.width, extent.height); size > 1; size /= 2) ++levels;

    // vkCmdBlitImage with linear filtering needs the format to support it (every desktop GPU does for RGBA8,
    // but the spec doesn't promise it — so ask).
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(m_ctx->physicalDevice, kTextureFormat, &formatProps);
    if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      throw std::runtime_error("RGBA8 doesn't support linear blits; mip generation needs another path");
    }

    // TRANSFER_DST: receives the upload and the blits. TRANSFER_SRC: each level is the blit source for the
    // next. SAMPLED: the shader reads it.
    m_texture = vk::Image(*m_ctx, {kTextureFormat, extent, levels,
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, kTextures[m_params.texture]});
    vk::Buffer staging(*m_ctx, image.sizeBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.write(image.pixels.data(), image.sizeBytes());

    vk::OneTimeCommands once(*m_ctx);
    const VkCommandBuffer cmd = once.cmd();
    const VkImage img = m_texture.handle();

    // 1. All levels: UNDEFINED -> TRANSFER_DST (nothing to wait for: the image is brand new).
    //    dst must cover *both* later writers: the copy (level 0) and the blits (levels 1..n). With only COPY
    //    here, sync validation reports WRITE_AFTER_WRITE: the blits could race the layout transition.
    transitionMips(cmd, img, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // 2. Copy the pixels into level 0. GL: glTextureSubImage2D.
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle(), img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 3. Each level i is a half-size linear blit of level i-1. GL: glGenerateTextureMipmap, one call.
    int32_t w = int32_t(extent.width), h = int32_t(extent.height);
    for (uint32_t i = 1; i < levels; ++i) {
      // Level i-1 was just written (by the copy or the previous blit): make it a readable blit source.
      transitionMips(cmd, img, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
      blit.srcOffsets[1] = {w, h, 1};
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
      blit.dstOffsets[1] = {std::max(w / 2, 1), std::max(h / 2, 1), 1};
      vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                     &blit, VK_FILTER_LINEAR);
      w = std::max(w / 2, 1);
      h = std::max(h / 2, 1);
    }

    // 4. Everything to SHADER_READ_ONLY for the fragment shader. Levels 0..n-2 are blit sources now, the last
    //    level is still a blit destination — two different old layouts, so two barriers.
    if (levels > 1) {
      transitionMips(cmd, img, 0, levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT);
    }
    transitionMips(cmd, img, levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    once.submitAndWait();  // `staging` is destroyed after this, once the GPU is done with it
    log::debug("03 texture {}: {}x{}, {} mip levels", kTextures[m_params.texture], extent.width, extent.height,
               levels);
  }

  // VkSamplers are immutable: one per filter mode, picked per frame via the descriptor.
  // GL's sampler object is mutable (03's gl.cpp changes its filter every frame instead).
  void createSamplers() {
    const struct {
      VkFilter filter;
      VkSamplerMipmapMode mipmap;
      float maxLod;
      const char* name;
    } modes[] = {
        {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, "nearest"},  // maxLod 0: level 0 only
        {VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, "linear"},
        {VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE, "trilinear"},
    };
    for (size_t i = 0; i < 3; ++i) {
      VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      info.magFilter = modes[i].filter;
      info.minFilter = modes[i].filter;
      info.mipmapMode = modes[i].mipmap;
      info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      info.maxLod = modes[i].maxLod;
      VK_CHECK(vkCreateSampler(m_ctx->device, &info, nullptr, &m_samplers[i]));
      vk::debug::setName(m_ctx->device, VK_OBJECT_TYPE_SAMPLER, m_samplers[i], modes[i].name);
    }
  }

  Params m_params;
  float m_angle = 0.0f;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_texture;
  vk::Image m_depth;
  std::array<VkSampler, 3> m_samplers{};  // indexed by Filter
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, vk::kFramesInFlight> m_sets{};
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("03_textured_cube", TexturedCubeVK);

}  // namespace glint::textured_cube
