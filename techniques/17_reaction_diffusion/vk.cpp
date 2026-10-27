// 17_reaction_diffusion — Vulkan. Two RGBA32F images, A and B, alternate as source and destination every step
// (ping-pong). Two ways to run a step:
//   compute:  STORAGE_IMAGE descriptors; both images stay in GENERAL — between steps only memory dependencies
//   fragment: render into dst (COLOR_ATTACHMENT) while sampling src (SHADER_READ_ONLY) — both images change
//             layout every step, because their roles swap
// A tiny CPU-side layout tracker (m_layout) knows where each image is, so one function records every transition:
// what engines do at scale ("render graphs"). Then a two-pass reduction writes total v into a host-visible buffer,
// read when this frame slot comes around again (its fence was waited on) — no stall. Shaders are shared with GL.

#include <array>
#include <initializer_list>
#include <vector>

#include "core/log.h"
#include "params.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::rd {

namespace {
constexpr VkFormat kFormat = VK_FORMAT_R32G32B32A32_SFLOAT;  // rg32f would need shaderStorageImageExtendedFormats
constexpr int kMaxGroups = (1024 / kGroup) * (1024 / kGroup);

// What each layout is used for here: which stages touch an image in that layout, and how.
struct Use {
  VkPipelineStageFlags2 stages;
  VkAccessFlags2 reads, writes;
};
Use useOf(VkImageLayout layout) {
  switch (layout) {
    case VK_IMAGE_LAYOUT_GENERAL:  // compute steps: imageLoad / imageStore
      return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:  // fragment step destination
      return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:  // fragment step source, reduction, display
      return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              VK_ACCESS_2_SHADER_READ_BIT, 0};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:  // CPU check
      return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 0};
    default:  // UNDEFINED: nothing happened yet
      return {VK_PIPELINE_STAGE_2_NONE, 0, 0};
  }
}

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}
}  // namespace

class ReactionDiffusionVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;

    // RGBA32F as storage image and color attachment is required by the spec; *linear filtering* of 32-bit floats is
    // optional (GL 4.x requires it). Ask, and display with nearest filtering if it's missing.
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, kFormat, &props);
    const bool linear = props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!linear) log::warn("17_reaction_diffusion: RGBA32F can't be filtered linearly here; display uses nearest");
    m_nearest = vk::createSampler(device, {.filter = VK_FILTER_NEAREST, .mipmaps = false, .name = "nearest repeat"});
    m_display = vk::createSampler(device, {.filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                           .mipmaps = false, .name = "display"});

    // --- descriptors ------------------------------------------------------------------------------------------
    // storage: (src, dst) for compute steps — one set per direction. sampled: one image read-only — one set per
    // image, used by the fragment step, the reduction and the display. reduce: partial sums + result buffer.
    constexpr VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
    m_storageLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute},
                                                   {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute}});
    m_sampledLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                  VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT}});
    m_reduceLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute}});
    m_pool = vk::createPool(device, 7,
                            {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
                             {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}});
    const auto storage = vk::allocateSets(device, m_pool, m_storageLayout, 2);
    const auto sampled = vk::allocateSets(device, m_pool, m_sampledLayout, 2);
    const auto display = vk::allocateSets(device, m_pool, m_sampledLayout, 2);  // same layout, filtering sampler
    std::copy(storage.begin(), storage.end(), m_storageSets.begin());
    std::copy(sampled.begin(), sampled.end(), m_sampledSets.begin());
    std::copy(display.begin(), display.end(), m_displaySets.begin());
    m_reduceSet = vk::allocateSets(device, m_pool, m_reduceLayout, 1).front();

    m_partials = vk::Buffer(ctx, kMaxGroups * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "17 reduction partials");
    // One result per frame slot, written by the GPU, read by the CPU through the persistent mapping.
    m_results = vk::Buffer(ctx, vk::kFramesInFlight * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           "17 reduction results (mapped)");
    vk::writeBuffer(device, m_reduceSet, 0, m_partials.handle(), m_partials.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    vk::writeBuffer(device, m_reduceSet, 1, m_results.handle(), m_results.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // --- pipelines ----------------------------------------------------------------------------------------------
    const std::string dir = "17_reaction_diffusion/";
    m_computeLayout = vk::createPipelineLayout(device, {m_storageLayout},
                                               {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(StepConstants)}});
    m_fragmentLayout = vk::createPipelineLayout(device, {m_sampledLayout},
                                                {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(StepConstants)}});
    m_reducePipelineLayout = vk::createPipelineLayout(device, {m_sampledLayout, m_reduceLayout},
                                                      {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ReduceConstants)}});
    m_displayLayout = vk::createPipelineLayout(device, {m_sampledLayout},
                                               {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DisplayConstants)}});
    m_seedPipeline = vk::createComputePipeline(device, dir + "seed.comp.spv", m_computeLayout, "17 seed");
    m_stepComputePipeline =
        vk::createComputePipeline(device, dir + "step.comp.spv", m_computeLayout, "17 step (compute)");
    m_reducePipeline =
        vk::createComputePipeline(device, dir + "reduce.comp.spv", m_reducePipelineLayout, "17 reduce");

    vk::GraphicsPipelineDesc step;
    step.vertexShader = "common/screen.vert.spv";
    step.fragmentShader = dir + "step.frag.spv";
    step.layout = m_fragmentLayout;
    step.colorFormat = kFormat;  // renders into the simulation image, not the swapchain
    step.name = "17 step (fragment)";
    m_stepFragmentPipeline = vk::createGraphicsPipeline(device, step);

    vk::GraphicsPipelineDesc displayDesc;
    displayDesc.vertexShader = "common/screen.vert.spv";
    displayDesc.fragmentShader = dir + "display.frag.spv";
    displayDesc.layout = m_displayLayout;
    displayDesc.colorFormat = swapchainFormat;
    displayDesc.name = "17 display";
    m_displayPipeline = vk::createGraphicsPipeline(device, displayDesc);

    m_readback = vk::Buffer(ctx, kCheckBlock * kCheckBlock * sizeof(glm::vec4), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "17 readback");
  }

  ~ReactionDiffusionVK() override {
    const VkDevice device = m_ctx->device;
    for (VkPipeline p : {m_seedPipeline, m_stepComputePipeline, m_reducePipeline, m_stepFragmentPipeline,
                         m_displayPipeline}) {
      vkDestroyPipeline(device, p, nullptr);
    }
    for (VkPipelineLayout l : {m_computeLayout, m_fragmentLayout, m_reducePipelineLayout, m_displayLayout}) {
      vkDestroyPipelineLayout(device, l, nullptr);
    }
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    for (VkDescriptorSetLayout l : {m_storageLayout, m_sampledLayout, m_reduceLayout}) {
      vkDestroyDescriptorSetLayout(device, l, nullptr);
    }
    vkDestroySampler(device, m_nearest, nullptr);
    vkDestroySampler(device, m_display, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    if (m_readbackPending) finishVerify();
    m_autoVerify.step(m_params);
    m_brushTexel = screenToTexel(frame.input.mousePos(), frame.framebufferSize, m_params.grid());
    m_brush = painting(frame.input) && !m_autoVerify.active();
    if (m_params.grid() != m_grid) createTargets(m_params.grid());
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    collectResult(info.frameInFlight);
    const glm::uvec2 groups = groupCounts(m_grid);
    const VkExtent2D gridExtent{uint32_t(m_grid.x), uint32_t(m_grid.y)};

    if (m_params.reset) {
      vk::debug::Label label(cmd, "seed");
      layouts(cmd, {{0, VK_IMAGE_LAYOUT_GENERAL, true}});  // discard: old contents don't matter
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_seedPipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1, &m_storageSets[1], 0,
                              nullptr);  // set 1 = (src B, dst A)
      const StepConstants sc = makeStepConstants(m_params, false, {});
      vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sc), &sc);
      vkCmdDispatch(cmd, groups.x, groups.y, 1);
      m_current = 0;
      m_steps = 0;
      m_painted = false;
      m_params.reset = false;
    }

    // --- the steps -----------------------------------------------------------------------------------------------
    const int steps = m_params.paused ? 0 : m_params.stepsPerFrame;
    {
      vk::debug::Label label(cmd, m_params.path == 0 ? "steps (compute)" : "steps (fragment)");
      for (int s = 0; s < steps; ++s) {
        const int src = m_current, dst = 1 - m_current;
        const StepConstants sc = makeStepConstants(m_params, m_brush && s == 0, m_brushTexel);
        if (m_params.path == 0) {
          // Both in GENERAL already after the first step: these are pure memory dependencies (the previous step
          // wrote src and read dst), no layout change. GL: glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT).
          layouts(cmd, {{src, VK_IMAGE_LAYOUT_GENERAL, false}, {dst, VK_IMAGE_LAYOUT_GENERAL, true}});
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_stepComputePipeline);
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1, &m_storageSets[src], 0,
                                  nullptr);
          vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sc), &sc);
          vkCmdDispatch(cmd, groups.x, groups.y, 1);
        } else {
          // The roles swap every step, so both images change layout every step. GL: nothing at all.
          layouts(cmd, {{src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false},
                        {dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true}});
          vk::beginRendering(cmd, gridExtent, {.color = m_images[size_t(dst)].view()});
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_stepFragmentPipeline);
          vk::setFlippedViewport(cmd, gridExtent);  // covers the grid; the step addresses texels by gl_FragCoord
          vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fragmentLayout, 0, 1, &m_sampledSets[src],
                                  0, nullptr);
          vkCmdPushConstants(cmd, m_fragmentLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sc), &sc);
          vkCmdDraw(cmd, 3, 1, 0, 0);
          vkCmdEndRendering(cmd);
        }
        m_painted = m_painted || (m_brush && s == 0);
        m_current = dst;
        ++m_steps;
      }
    }
    const int cur = m_current;
    if (m_params.verify) recordReadback(cmd, cur);
    layouts(cmd, {{cur, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false}});

    // --- reduction: total v → result[frame slot] ----------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "reduce total v");
      // partials: last frame's pass 1 read them and pass 0 wrote them (WAR + WAW); same queue, so a barrier.
      memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reducePipeline);
      const VkDescriptorSet sets[] = {m_sampledSets[size_t(cur)], m_reduceSet};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reducePipelineLayout, 0, 2, sets, 0, nullptr);
      ReduceConstants rc{glm::uvec4(uint32_t(m_grid.x), uint32_t(m_grid.y), 0u, info.frameInFlight)};
      vkCmdPushConstants(cmd, m_reducePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rc), &rc);
      vkCmdDispatch(cmd, groups.x, groups.y, 1);
      // Pass 1 reads the partials — but the *same* shader also contains pass 0's write to them (and pass 0
      // contains the result write): which buffers a dispatch may write is decided per shader, not per branch taken.
      // So the dependency must cover write-after-write too. (Sync validation flagged READ-only here.)
      memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
      rc.info.z = 1;
      vkCmdPushConstants(cmd, m_reducePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rc), &rc);
      vkCmdDispatch(cmd, 1, 1, 1);
      // GPU write → CPU read of the mapped memory, once this slot's fence signals. GL: CLIENT_MAPPED + a fence.
      memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
      m_resultWritten[info.frameInFlight] = true;
    }

    // --- display ----------------------------------------------------------------------------------------------------
    vk::beginRendering(cmd, info.extent, {.color = info.targetView});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayPipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayLayout, 0, 1, &m_displaySets[size_t(cur)],
                            0, nullptr);
    const DisplayConstants dc{glm::vec4(info.frame.aspect(), float(m_params.palette), 0, 0),
                              glm::vec4(m_brushTexel, m_params.brushRadius, 1.0f)};
    vkCmdPushConstants(cmd, m_displayLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dc), &dc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params, m_history, vk::kFramesInFlight, m_verifyResult); }

 private:
  struct Target {
    int image;
    VkImageLayout layout;
    bool discard;  // the next use overwrites everything: old contents (and old layout) don't matter
  };

  // The layout tracker: move images to the layouts the next command needs, in one vkCmdPipelineBarrier2. Always
  // emits a barrier, even when the layout doesn't change: the previous use may still be a hazard (a compute step
  // wrote src, or read dst). src side = how the image was used in its current layout, dst side = the new one.
  void layouts(VkCommandBuffer cmd, std::initializer_list<Target> targets) {
    std::array<VkImageMemoryBarrier2, 2> barriers{};
    uint32_t n = 0;
    for (const Target& t : targets) {
      const VkImageLayout old = m_layout[size_t(t.image)];
      const Use from = useOf(old), to = useOf(t.layout);
      VkImageMemoryBarrier2& b = barriers[n++];
      b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
      b.srcStageMask = from.stages;
      b.srcAccessMask = from.writes;  // only writes need to be made available; reads just need ordering
      b.dstStageMask = to.stages;
      b.dstAccessMask = to.reads | to.writes;
      b.oldLayout = t.discard && old != t.layout ? VK_IMAGE_LAYOUT_UNDEFINED : old;
      b.newLayout = t.layout;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = m_images[size_t(t.image)].handle();
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      m_layout[size_t(t.image)] = t.layout;
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = n;
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmd, &dependency);
  }

  // This slot's fence was waited on before record(), so the value it wrote last time is final.
  void collectResult(uint32_t slot) {
    if (!m_resultWritten[slot]) return;
    m_history.push(static_cast<const float*>(m_results.mapped())[slot]);
  }

  void createTargets(glm::ivec2 grid) {
    const VkDevice device = m_ctx->device;
    VK_CHECK(vkDeviceWaitIdle(device));
    m_grid = grid;
    const VkExtent2D extent{uint32_t(grid.x), uint32_t(grid.y)};
    for (int i = 0; i < 2; ++i) {
      // Every role up front: storage (compute steps, seed), sampled (fragment steps, reduce, display), color
      // attachment (fragment steps), transfer source (CPU check). GL decides per bind.
      m_images[size_t(i)] = vk::Image(*m_ctx, {kFormat, extent, 1,
                                               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                               VK_IMAGE_ASPECT_COLOR_BIT, i == 0 ? "17 state A" : "17 state B"});
      m_layout[size_t(i)] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    for (int i = 0; i < 2; ++i) {
      const VkImageView src = m_images[size_t(i)].view(), dst = m_images[size_t(1 - i)].view();
      std::array<VkDescriptorImageInfo, 2> infos{{{VK_NULL_HANDLE, src, VK_IMAGE_LAYOUT_GENERAL},
                                                  {VK_NULL_HANDLE, dst, VK_IMAGE_LAYOUT_GENERAL}}};
      std::array<VkWriteDescriptorSet, 2> writes{};
      for (uint32_t b = 0; b < 2; ++b) {
        writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[b].dstSet = m_storageSets[size_t(i)];
        writes[b].dstBinding = b;
        writes[b].descriptorCount = 1;
        writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[b].pImageInfo = &infos[b];
      }
      vkUpdateDescriptorSets(device, 2, writes.data(), 0, nullptr);
      vk::writeImage(device, m_sampledSets[size_t(i)], 0, src, m_nearest);
      vk::writeImage(device, m_displaySets[size_t(i)], 0, src, m_display);
    }
    m_params.reset = true;
  }

  void recordReadback(VkCommandBuffer cmd, int cur) {
    if (m_painted) {  // the CPU reference knows nothing about brush strokes
      m_verifyResult = "skipped: painted since the last reset (reset, then verify before painting)";
      m_params.verify = false;
      return;
    }
    layouts(cmd, {{cur, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false}});
    const glm::ivec2 o = checkOrigin(m_grid);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {o.x, o.y, 0};
    region.imageExtent = {kCheckBlock, kCheckBlock, 1};
    vkCmdCopyImageToBuffer(cmd, m_images[size_t(cur)].handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_readback.handle(), 1, &region);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                  VK_ACCESS_2_HOST_READ_BIT);
    m_readbackSteps = m_steps;
    m_readbackPath = m_params.path;
    m_readbackPending = true;
    m_params.verify = false;
  }

  void finishVerify() {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
    m_verifyResult =
        verifyBlock(static_cast<const glm::vec4*>(m_readback.mapped()), m_params, m_readbackSteps, m_readbackPath);
    log::info("17_reaction_diffusion verify: {}", m_verifyResult);
    m_readbackPending = false;
  }

  Params m_params;
  AutoVerify m_autoVerify;
  History m_history;
  int m_steps = 0, m_current = 0;
  bool m_brush = false, m_painted = false;
  glm::vec2 m_brushTexel{0.0f};
  glm::ivec2 m_grid{0};
  vk::Context* m_ctx = nullptr;
  std::array<vk::Image, 2> m_images;
  std::array<VkImageLayout, 2> m_layout{};
  VkSampler m_nearest = VK_NULL_HANDLE, m_display = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_storageLayout = VK_NULL_HANDLE, m_sampledLayout = VK_NULL_HANDLE,
                        m_reduceLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, 2> m_storageSets{}, m_sampledSets{}, m_displaySets{};
  VkDescriptorSet m_reduceSet = VK_NULL_HANDLE;
  VkPipelineLayout m_computeLayout = VK_NULL_HANDLE, m_fragmentLayout = VK_NULL_HANDLE,
                   m_reducePipelineLayout = VK_NULL_HANDLE, m_displayLayout = VK_NULL_HANDLE;
  VkPipeline m_seedPipeline = VK_NULL_HANDLE, m_stepComputePipeline = VK_NULL_HANDLE, m_reducePipeline = VK_NULL_HANDLE,
             m_stepFragmentPipeline = VK_NULL_HANDLE, m_displayPipeline = VK_NULL_HANDLE;
  vk::Buffer m_partials, m_results, m_readback;
  std::array<bool, vk::kFramesInFlight> m_resultWritten{};
  int m_readbackSteps = 0, m_readbackPath = 0;
  bool m_readbackPending = false;
  std::string m_verifyResult;
};

GLINT_REGISTER_VK("17_reaction_diffusion", ReactionDiffusionVK);

}  // namespace glint::rd
