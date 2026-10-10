// 14_compute_particles — Vulkan. Per frame, all in the one graphics+compute queue's command buffer:
//   1. buffer barrier: last frame's vertex reads (and compute writes) -> this frame's compute
//   2. compute: vkCmdDispatch over the particle storage buffer (integrate in place)
//   3. buffer barrier: compute writes -> vertex attribute reads
//   4. draw: the same buffer as a vertex buffer, POINT_LIST, additive into an RGBA16F image
//   5. tone map into the swapchain image (12's shader)
// The compute pipeline is written out raw here (first use); shaders are shared with GL.

#include "../12_hdr_tonemapping/params.h"
#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/shader.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::particles {

namespace {
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// A buffer memory barrier (synchronization2), written out once here: "after these stages did these accesses
// to `buffer`, before these stages do these accesses". GL's glMemoryBarrier only names the *dst* side, for
// all buffers at once; the src side is implied (all earlier incoherent writes).
void bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;  // same queue: no ownership transfer
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer;
  barrier.offset = 0;
  barrier.size = VK_WHOLE_SIZE;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.bufferMemoryBarrierCount = 1;
  dependency.pBufferMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}
}  // namespace

class ParticlesVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome, kCameraTarget); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;

    // One particle buffer, not one per frame in flight: only the GPU reads and writes it, and the barriers
    // order frame N+1's compute after frame N's draw. (Per-frame copies are for data the *CPU* writes.)
    // Usage lists both roles up front; GL lets any buffer be bound anywhere.
    m_particles = vk::Buffer(ctx, kMaxParticles * sizeof(Particle),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "14 particles");
    m_hdrSampler = vk::createSampler(
        device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .name = "linear clamp"});

    // --- descriptors: the SSBO for compute, the HDR image for the tone map --------------------------------
    m_simSetLayout =
        vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT}});
    m_tonemapSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, 2,
                            {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}});
    m_simSet = vk::allocateSets(device, m_pool, m_simSetLayout, 1).front();
    m_tonemapSet = vk::allocateSets(device, m_pool, m_tonemapSetLayout, 1).front();
    vk::writeBuffer(device, m_simSet, 0, m_particles.handle(), m_particles.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    m_simLayout = vk::createPipelineLayout(device, {m_simSetLayout},
                                           {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SimConstants)}});
    m_drawLayout = vk::createPipelineLayout(device, {}, {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DrawConstants)}});
    m_tonemapLayout = vk::createPipelineLayout(device, {m_tonemapSetLayout},
                                               {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hdr::TonemapConstants)}});

    // --- the compute pipeline, raw ------------------------------------------------------------------------
    // Compare with createGraphicsPipeline: one shader stage and a layout, nothing else. No vertex input,
    // rasterizer, blend or attachment formats — and no render pass/rendering: dispatches happen *outside*
    // vkCmdBeginRendering. GL: a program with one compute shader.
    VkShaderModule module = vk::loadShaderModule(device, "14_compute_particles/simulate.comp.spv");
    VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                         module, "main"};
    computeInfo.layout = m_simLayout;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &m_simPipeline));
    vkDestroyShaderModule(device, module, nullptr);
    vk::debug::setName(device, VK_OBJECT_TYPE_PIPELINE, m_simPipeline, "14 simulate (compute)");

    // --- the point draw: the particle buffer *is* the vertex buffer -------------------------------------------
    vk::GraphicsPipelineDesc draw;
    draw.vertexShader = "14_compute_particles/particle.vert.spv";
    draw.fragmentShader = "14_compute_particles/particle.frag.spv";
    draw.vertexInput.bindings = {{0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX}};
    draw.vertexInput.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                   {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4)}};
    draw.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;  // GL: glDrawArrays(GL_POINTS, ...)
    draw.layout = m_drawLayout;
    draw.colorFormat = kHdrFormat;
    draw.additiveBlend = true;
    draw.name = "14 particles (points)";
    m_drawPipeline = vk::createGraphicsPipeline(device, draw);

    vk::GraphicsPipelineDesc tonemap;
    tonemap.vertexShader = "common/fullscreen.vert.spv";
    tonemap.fragmentShader = "12_hdr_tonemapping/tonemap.frag.spv";
    tonemap.layout = m_tonemapLayout;
    tonemap.colorFormat = swapchainFormat;
    tonemap.name = "14 tonemap";
    m_tonemapPipeline = vk::createGraphicsPipeline(device, tonemap);
  }

  ~ParticlesVK() override {
    const VkDevice device = m_ctx->device;
    for (VkPipeline p : {m_simPipeline, m_drawPipeline, m_tonemapPipeline}) vkDestroyPipeline(device, p, nullptr);
    for (VkPipelineLayout l : {m_simLayout, m_drawLayout, m_tonemapLayout}) vkDestroyPipelineLayout(device, l, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_simSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_tonemapSetLayout, nullptr);
    vkDestroySampler(device, m_hdrSampler, nullptr);
  }

  void update(float dt, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_hdr.extent().width || size.height != m_hdr.extent().height) createTargets(size);
    m_dt = simDt(dt, m_params);
    m_simTime += m_dt;
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const VkBuffer particles = m_particles.handle();

    if (shouldSimulate(m_params)) {
      vk::debug::Label label(cmd, "simulate (compute)");
      // --- 1. previous frame's uses -> this dispatch ---------------------------------------------------------
      // Write-after-read against last frame's vertex fetch, and write-after-write against last frame's compute.
      // GL needs nothing here: incoherent writes only need a barrier before they are *read*.
      bufferBarrier(cmd, particles,
                    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

      // --- 2. dispatch ---------------------------------------------------------------------------------------
      // Compute has its own bind point: binding the compute pipeline/sets leaves the graphics ones alone.
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_simPipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_simLayout, 0, 1, &m_simSet, 0, nullptr);
      const SimConstants sc = makeSimConstants(m_params, m_dt, m_simTime);
      vkCmdPushConstants(cmd, m_simLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sc), &sc);
      vkCmdDispatch(cmd, groupCount(m_params), 1, 1);

      // --- 3. compute writes -> vertex attribute reads (GL: glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT))
      bufferBarrier(cmd, particles, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
      m_params.reset = false;
    }

    // --- 4. draw the particles into the HDR image ---------------------------------------------------------------
    vk::transitionImage(cmd, m_hdr.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    {
      vk::debug::Label label(cmd, "particles (points)");
      vk::beginRendering(cmd, m_hdr.extent(), {.color = m_hdr.view(), .clearColor = {0, 0, 0, 1}});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawPipeline);
      vk::setFlippedViewport(cmd, m_hdr.extent());
      vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
      const DrawConstants dc{info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne),
                             glm::vec4(m_params.brightness, 0.0f, 0.0f, 0.0f)};
      vkCmdPushConstants(cmd, m_drawLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(dc), &dc);
      const VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &particles, &offset);
      vkCmdDraw(cmd, m_params.count(), 1, 0, 0);
      vkCmdEndRendering(cmd);
    }
    vk::transitionImage(cmd, m_hdr.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);

    // --- 5. tone map -----------------------------------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "tone map");
      vk::beginRendering(cmd, info.extent, {.color = info.targetView});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline);
      vk::setFlippedViewport(cmd, info.extent);
      vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapLayout, 0, 1, &m_tonemapSet, 0, nullptr);
      const hdr::TonemapConstants tc{glm::vec4(std::exp2(m_params.exposureEv), 2.0f, 1.0f, 0.0f)};
      vkCmdPushConstants(cmd, m_tonemapLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tc), &tc);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      vkCmdEndRendering(cmd);
    }
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(VkExtent2D size) {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
    m_hdr = vk::Image(*m_ctx, {kHdrFormat, size, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, "14 hdr color"});
    vk::writeImage(m_ctx->device, m_tonemapSet, 0, m_hdr.view(), m_hdrSampler);
  }

  Params m_params;
  float m_dt = 0.0f, m_simTime = 0.0f;
  vk::Context* m_ctx = nullptr;
  vk::Buffer m_particles;
  vk::Image m_hdr;
  VkSampler m_hdrSampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_simSetLayout = VK_NULL_HANDLE, m_tonemapSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  VkDescriptorSet m_simSet = VK_NULL_HANDLE, m_tonemapSet = VK_NULL_HANDLE;
  VkPipelineLayout m_simLayout = VK_NULL_HANDLE, m_drawLayout = VK_NULL_HANDLE, m_tonemapLayout = VK_NULL_HANDLE;
  VkPipeline m_simPipeline = VK_NULL_HANDLE, m_drawPipeline = VK_NULL_HANDLE, m_tonemapPipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("14_compute_particles", ParticlesVK);

}  // namespace glint::particles
