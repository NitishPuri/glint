// 16_noise — Vulkan. A compute shader writes four noises into a 3D R16G16B16A16_SFLOAT image through a
// STORAGE_IMAGE descriptor (image in the GENERAL layout), then a full-screen pass samples the same image through
// a COMBINED_IMAGE_SAMPLER descriptor (SHADER_READ_ONLY layout). Regenerated only when the generation parameters
// change (or every frame with "evolve"). The 3D image and the storage-image descriptor are written out raw here:
// vk::Image only does 2D. Shaders are shared with GL.

#include <glm/gtc/packing.hpp>
#include <optional>

#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::noise {

namespace {
constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // GL: GL_RGBA16F; GLSL: rgba16f
}  // namespace

class NoiseVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;

    // Can this format be a storage image and be sampled with linear filtering? (RGBA16F storage is required by
    // the spec, but asking is the habit: R16F, for example, needs shaderStorageImageExtendedFormats.)
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, kFormat, &props);
    constexpr VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((props.optimalTilingFeatures & needed) != needed) {
      throw std::runtime_error("16_noise: RGBA16F can't be a storage image + linearly sampled on this device");
    }
    m_sampler = vk::createSampler(device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                           .name = "trilinear repeat (3D)"});

    // Two descriptor sets for the same image: written as a storage image, read as a sampled one.
    m_generateSetLayout =
        vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}});
    m_displaySetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, 2,
                            {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}});
    m_generateSet = vk::allocateSets(device, m_pool, m_generateSetLayout, 1).front();
    m_displaySet = vk::allocateSets(device, m_pool, m_displaySetLayout, 1).front();

    m_generateLayout = vk::createPipelineLayout(device, {m_generateSetLayout},
                                                {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GenerateConstants)}});
    m_displayLayout = vk::createPipelineLayout(device, {m_displaySetLayout},
                                               {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DisplayConstants)}});
    m_generatePipeline =
        vk::createComputePipeline(device, "16_noise/generate.comp.spv", m_generateLayout, "16 generate noise");
    vk::GraphicsPipelineDesc display;
    display.vertexShader = "16_noise/screen.vert.spv";
    display.fragmentShader = "16_noise/display.frag.spv";
    display.layout = m_displayLayout;
    display.colorFormat = swapchainFormat;
    display.name = "16 display";
    m_displayPipeline = vk::createGraphicsPipeline(device, display);

    m_readback = vk::Buffer(ctx, kCheckBlock * kCheckBlock * kCheckBlock * 4 * sizeof(uint16_t),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "16 readback");
  }

  ~NoiseVK() override {
    const VkDevice device = m_ctx->device;
    destroyImage();
    vkDestroyPipeline(device, m_generatePipeline, nullptr);
    vkDestroyPipeline(device, m_displayPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_generateLayout, nullptr);
    vkDestroyPipelineLayout(device, m_displayLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_generateSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_displaySetLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
  }

  void update(float dt, Frame& /*frame*/) override {
    if (m_readbackPending) finishVerify();
    m_time += dt;
    if (m_params.evolve) m_params.gen.evolveOffset += dt * m_params.evolveSpeed;
    if (++m_frame == 10) m_params.verify = true;
    if (m_params.gen.extent() != m_extent) createImage(m_params.gen.extent());
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const Generation& gen = m_params.gen;
    if (!m_generated || !(*m_generated == gen)) generate(cmd, gen);
    if (m_params.verify) recordReadback(cmd, gen);

    vk::debug::Label label(cmd, "display");
    vk::beginRendering(cmd, info.extent, {.color = info.targetView});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayPipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayLayout, 0, 1, &m_displaySet, 0, nullptr);
    const DisplayConstants dc =
        makeDisplayConstants(m_params, info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne),
                             info.frame.camera.eye, info.frame.aspect(), m_time);
    vkCmdPushConstants(cmd, m_displayLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dc), &dc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params, m_verifyResult); }

 private:
  // --- the 3D image, raw ---------------------------------------------------------------------------------------
  void createImage(int n) {
    const VkDevice device = m_ctx->device;
    VK_CHECK(vkDeviceWaitIdle(device));
    destroyImage();
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_3D;  // vs 2D: extent.depth is a real dimension, arrayLayers stays 1
    info.format = kFormat;
    info.extent = {uint32_t(n), uint32_t(n), uint32_t(n)};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    // Every role up front: written by compute (STORAGE), read by the display pass (SAMPLED), copied out for the
    // CPU check (TRANSFER_SRC). GL's texture can be bound as an image or a texture without declaring anything.
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &info, nullptr, &m_image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, m_image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = m_ctx->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &m_memory));
    VK_CHECK(vkBindImageMemory(device, m_image, m_memory, 0));
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;  // GLSL image3D / sampler3D
    viewInfo.format = kFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_view));
    vk::debug::setName(device, VK_OBJECT_TYPE_IMAGE, m_image, "16 noise 3D");
    vk::debug::setName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_view, "16 noise 3D");

    // The storage-image descriptor, raw (vk::writeImage only does combined image samplers): no sampler, and the
    // layout the image will be in *when the shader runs* — GENERAL, the only layout storage access allows.
    const VkDescriptorImageInfo storage{VK_NULL_HANDLE, m_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_generateSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &storage;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    vk::writeImage(device, m_displaySet, 0, m_view, m_sampler);  // SHADER_READ_ONLY_OPTIMAL

    m_extent = n;
    m_generated.reset();
  }

  void destroyImage() {
    const VkDevice device = m_ctx->device;
    vkDestroyImageView(device, m_view, nullptr);
    vkDestroyImage(device, m_image, nullptr);
    vkFreeMemory(device, m_memory, nullptr);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
  }

  void generate(VkCommandBuffer cmd, const Generation& gen) {
    vk::debug::Label label(cmd, "generate noise (compute)");
    // UNDEFINED → GENERAL: old contents discarded. Ordered after earlier frames' sampling (write-after-read,
    // execution only) and copies.
    vk::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_generatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_generateLayout, 0, 1, &m_generateSet, 0, nullptr);
    const GenerateConstants gc = makeGenerateConstants(gen);
    vkCmdPushConstants(cmd, m_generateLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gc), &gc);
    const glm::uvec3 groups = groupCounts(gen);
    vkCmdDispatch(cmd, groups.x, groups.y, groups.z);
    // GENERAL → SHADER_READ_ONLY: storage writes visible to fragment-shader sampling (GL:
    // glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT)). Sampling in GENERAL would also be legal, and skip this
    // layout change; READ_ONLY is the layout drivers may compress or swizzle for.
    vk::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);
    m_generated = gen;
  }

  // --- the CPU check: copy a block out as halfs, convert after the GPU is done ---------------------------------
  void recordReadback(VkCommandBuffer cmd, const Generation& gen) {
    vk::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const glm::ivec3 o = checkOrigin(gen);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {o.x, o.y, o.z};
    region.imageExtent = {kCheckBlock, kCheckBlock, kCheckBlock};
    vkCmdCopyImageToBuffer(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.handle(), 1, &region);
    vk::transitionImage(cmd, m_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    VkMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &toHost;
    vkCmdPipelineBarrier2(cmd, &dependency);
    m_readbackGen = gen;
    m_readbackPending = true;
    m_params.verify = false;
  }

  void finishVerify() {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
    const auto* halfs = static_cast<const uint16_t*>(m_readback.mapped());
    std::vector<glm::vec4> texels(kCheckBlock * kCheckBlock * kCheckBlock);
    for (size_t i = 0; i < texels.size(); ++i) {
      for (int c = 0; c < 4; ++c) texels[i][c] = glm::unpackHalf1x16(halfs[i * 4 + size_t(c)]);
    }
    m_verifyResult = verifyBlock(texels.data(), m_readbackGen);
    log::info("16_noise verify: {}", m_verifyResult);
    m_readbackPending = false;
  }

  Params m_params;
  std::optional<Generation> m_generated;  // what the image holds (recorded; the GPU catches up)
  float m_time = 0.0f;
  int m_frame = 0;
  vk::Context* m_ctx = nullptr;
  VkImage m_image = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkImageView m_view = VK_NULL_HANDLE;
  int m_extent = 0;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_generateSetLayout = VK_NULL_HANDLE, m_displaySetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  VkDescriptorSet m_generateSet = VK_NULL_HANDLE, m_displaySet = VK_NULL_HANDLE;
  VkPipelineLayout m_generateLayout = VK_NULL_HANDLE, m_displayLayout = VK_NULL_HANDLE;
  VkPipeline m_generatePipeline = VK_NULL_HANDLE, m_displayPipeline = VK_NULL_HANDLE;
  vk::Buffer m_readback;
  Generation m_readbackGen;
  bool m_readbackPending = false;
  std::string m_verifyResult;
};

GLINT_REGISTER_VK("16_noise", NoiseVK);

}  // namespace glint::noise
