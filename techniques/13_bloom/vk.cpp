// 13_bloom — Vulkan. 12's HDR room, then a bloom chain and a graded composite:
//   pass 1        scene -> R16G16B16A16_SFLOAT
//   pass 2..L+1   downsample: HDR -> bloom level 0 (prefilter) -> level 1 -> ... -> level L-1
//   pass ..       upsample:   level L-1 -> L-2 -> ... -> 0, each *added* (blend ONE, ONE) onto the level
//   last          composite:  HDR + bloom, tone map, sRGB encode, grading LUT -> swapchain image
// The bloom levels are the mip levels of one VkImage, with one VkImageView per level. Levels of one image
// can be in *different layouts* at the same time, and every pass needs a barrier on exactly the level it
// touches: that per-level choreography (absent in GL) is what this file is about. Shaders are shared with GL.

#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/mesh.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::bloom {

namespace {
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkShaderStageFlags kSceneStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kLevels = kMaxLevels;

// Short names for the two barriers every bloom level goes through, so each call site reads as one line.
// "attachment -> sampled": the pass that wrote level `mip` is done; the next pass samples it.
void attachmentToSampled(VkCommandBuffer cmd, VkImage image, uint32_t mip) {
  vk::transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_READ_BIT, mip, 1);
}
}  // namespace

class BloomVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(hdr::kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_room = vk::Mesh(ctx, room, "room");
    m_cube = vk::Mesh(ctx, makeCube(), "light marker");
    m_albedo = vk::createTexture(ctx, loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap (sRGB)", /*srgb*/ true);
    m_albedoSampler = vk::createSampler(device, {.name = "trilinear repeat"});
    m_linearClamp = vk::createSampler(
        device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .name = "linear clamp"});

    // The grading LUT, plus one host-visible staging buffer per frame in flight for re-baking it (see record()).
    const ImageData lut = makeGradingLut(m_params.grading);
    m_lut = vk::createTexture(ctx, lut, false, "13 grading LUT");
    m_lutGrading = m_params.grading;
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_lutStaging[i] = vk::Buffer(ctx, lut.sizeBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   fmt::format("13 LUT staging {}", i));
    }

    // Bloom format. GL_R11F_G11F_B10F is required to be renderable in GL 4.x, so GL just uses it. In VK,
    // B10G11R11_UFLOAT_PACK32 is only guaranteed for sampling; rendering and *blending* into it are optional
    // features, so ask (RADV and NVIDIA support it; fall back to RGBA16F otherwise).
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &props);
    constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    m_bloomFormat = (props.optimalTilingFeatures & needed) == needed ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : kHdrFormat;
    if (m_bloomFormat == kHdrFormat) log::warn("13_bloom: B10G11R11_UFLOAT not renderable+blendable, using RGBA16F");

    // --- descriptor set layouts -----------------------------------------------------------------------------
    // scene: 12's (UBO + albedo). post: one source image (every downsample/upsample pass). composite: HDR,
    // bloom level 0, LUT. GL has none of this: units 0..2 are just bound before each draw.
    m_sceneSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kSceneStages},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_postSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_compositeSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                 {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});

    // One post set per pass: downsample i reads (i == 0 ? HDR : level i-1), upsample i reads level i+1. They
    // only change on resize, so they are written once there instead of every frame.
    const uint32_t postSets = kLevels + (kLevels - 1);
    m_pool = vk::createPool(device, vk::kFramesInFlight + postSets + 1,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight + postSets + 3}});
    m_sceneSets = vk::allocateSets(device, m_pool, m_sceneSetLayout, vk::kFramesInFlight);
    m_downSets = vk::allocateSets(device, m_pool, m_postSetLayout, kLevels);
    m_upSets = vk::allocateSets(device, m_pool, m_postSetLayout, kLevels - 1);
    m_compositeSet = vk::allocateSets(device, m_pool, m_compositeSetLayout, 1).front();
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_sceneUbo[i] = vk::Buffer(ctx, sizeof(hdr::SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 fmt::format("13 scene {}", i));
      vk::writeBuffer(device, m_sceneSets[i], 0, m_sceneUbo[i].handle(), sizeof(hdr::SceneUniforms));
      vk::writeImage(device, m_sceneSets[i], 1, m_albedo.view(), m_albedoSampler);
    }

    // Down- and upsample push constants are both one vec4, so one layout serves both pipelines.
    static_assert(sizeof(DownsampleConstants) == sizeof(UpsampleConstants));
    m_sceneLayout =
        vk::createPipelineLayout(device, {m_sceneSetLayout}, {{kSceneStages, 0, sizeof(hdr::DrawConstants)}});
    m_postLayout = vk::createPipelineLayout(device, {m_postSetLayout},
                                            {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DownsampleConstants)}});
    m_compositeLayout = vk::createPipelineLayout(device, {m_compositeSetLayout},
                                                 {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositeConstants)}});

    vk::GraphicsPipelineDesc scene;  // 12's scene shaders, unchanged
    scene.vertexShader = "12_hdr_tonemapping/scene.vert.spv";
    scene.fragmentShader = "12_hdr_tonemapping/scene.frag.spv";
    scene.vertexInput = m_room.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    scene.layout = m_sceneLayout;
    scene.colorFormat = kHdrFormat;
    scene.depthFormat = kDepthFormat;
    scene.name = "13 scene (HDR)";
    m_scenePipeline = vk::createGraphicsPipeline(device, scene);

    vk::GraphicsPipelineDesc down;
    down.vertexShader = "common/fullscreen.vert.spv";
    down.fragmentShader = "13_bloom/downsample.frag.spv";
    down.layout = m_postLayout;
    down.colorFormat = m_bloomFormat;
    down.name = "13 downsample";
    m_downPipeline = vk::createGraphicsPipeline(device, down);

    vk::GraphicsPipelineDesc up = down;
    up.fragmentShader = "13_bloom/upsample.frag.spv";
    up.additiveBlend = true;  // GL: glEnable(GL_BLEND) + glBlendFunc(GL_ONE, GL_ONE) around the loop
    up.name = "13 upsample (additive)";
    m_upPipeline = vk::createGraphicsPipeline(device, up);

    vk::GraphicsPipelineDesc composite;
    composite.vertexShader = "common/fullscreen.vert.spv";
    composite.fragmentShader = "13_bloom/composite.frag.spv";
    composite.layout = m_compositeLayout;
    composite.colorFormat = swapchainFormat;
    composite.name = "13 composite";
    m_compositePipeline = vk::createGraphicsPipeline(device, composite);
  }

  ~BloomVK() override {
    const VkDevice device = m_ctx->device;
    destroyLevelViews();
    for (VkPipeline p : {m_scenePipeline, m_downPipeline, m_upPipeline, m_compositePipeline}) {
      vkDestroyPipeline(device, p, nullptr);
    }
    for (VkPipelineLayout l : {m_sceneLayout, m_postLayout, m_compositeLayout}) {
      vkDestroyPipelineLayout(device, l, nullptr);
    }
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    for (VkDescriptorSetLayout l : {m_sceneSetLayout, m_postSetLayout, m_compositeSetLayout}) {
      vkDestroyDescriptorSetLayout(device, l, nullptr);
    }
    vkDestroySampler(device, m_albedoSampler, nullptr);
    vkDestroySampler(device, m_linearClamp, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_hdr.extent().width || size.height != m_hdr.extent().height) createTargets(size);
    m_params.scene.linearWorkflow = true;
    if (m_params.grading != m_lutGrading) {  // re-bake on the CPU now, upload in record()
      m_lutPixels = makeGradingLut(m_params.grading);
      m_lutGrading = m_params.grading;
      m_lutDirty = true;
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    m_sceneUbo[info.frameInFlight].write(hdr::makeSceneUniforms(
        info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne), info.frame.camera.eye,
        m_params.scene));
    if (m_lutDirty) uploadLut(cmd, info.frameInFlight);

    // --- pass 1: scene -> HDR, exactly as 12 ----------------------------------------------------------------
    vk::transitionImage(cmd, m_hdr.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    vk::transitionDepthForRendering(cmd, m_depth.handle());
    {
      vk::debug::Label label(cmd, "pass 1: scene (HDR)");
      vk::beginRendering(cmd, m_hdr.extent(),
                         {.color = m_hdr.view(), .clearColor = {0, 0, 0, 1}, .depth = m_depth.view()});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_scenePipeline);
      vk::setFlippedViewport(cmd, m_hdr.extent());
      vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1,
                              &m_sceneSets[info.frameInFlight], 0, nullptr);
      const hdr::DrawConstants roomDraw{glm::mat4(1.0f), glm::vec4(0.0f)};
      vkCmdPushConstants(cmd, m_sceneLayout, kSceneStages, 0, sizeof(roomDraw), &roomDraw);
      m_room.draw(cmd);
      for (int i = 0; i < hdr::kLights; ++i) {
        const hdr::DrawConstants marker = hdr::lightMarker(m_params.scene, i);
        vkCmdPushConstants(cmd, m_sceneLayout, kSceneStages, 0, sizeof(marker), &marker);
        m_cube.draw(cmd);
      }
      vkCmdEndRendering(cmd);
    }
    vk::transitionImage(cmd, m_hdr.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);

    // All bloom levels -> attachment, discarding last frame's contents. Ordered after last frame's passes,
    // which both wrote (attachment) and read (fragment shader) these levels.
    vk::transitionImage(cmd, m_bloom.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    const uint32_t levels = uint32_t(m_params.levels);
    const glm::ivec2 screen{int(m_hdr.extent().width), int(m_hdr.extent().height)};

    // --- downsample chain: write level i, then barrier it so level i+1's pass can sample it -----------------
    {
      vk::debug::Label label(cmd, "bloom downsample");
      for (uint32_t i = 0; i < levels; ++i) {
        const VkExtent2D extent = levelExtent(screen, i);
        vk::beginRendering(cmd, extent, {.color = m_levelViews[i]});  // clear is redundant but cheap
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_downPipeline);
        vk::setFlippedViewport(cmd, extent);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postLayout, 0, 1, &m_downSets[i], 0, nullptr);
        const DownsampleConstants dc = makeDownsampleConstants(m_params, i == 0);
        vkCmdPushConstants(cmd, m_postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dc), &dc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        attachmentToSampled(cmd, m_bloom.handle(), i);  // GL: nothing
      }
    }

    // --- upsample chain: level i (attachment, loaded) += tent(level i+1 (sampled)) --------------------------
    {
      vk::debug::Label label(cmd, "bloom upsample");
      for (uint32_t i = levels - 1; i-- > 0;) {  // i = levels-2 ... 0
        // Level i was last *sampled* (by downsample i+1) and before that *written* (downsample i). Back to an
        // attachment, keeping its contents (old layout SHADER_READ_ONLY, not UNDEFINED), and ordered so the
        // blend can read the earlier write.
        vk::transitionImage(cmd, m_bloom.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, i, 1);
        const VkExtent2D extent = levelExtent(screen, i);
        vk::beginRendering(cmd, extent, {.color = m_levelViews[i], .loadColor = true});  // LOAD: we add to it
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_upPipeline);
        vk::setFlippedViewport(cmd, extent);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postLayout, 0, 1, &m_upSets[i], 0, nullptr);
        const UpsampleConstants uc{glm::vec4(m_params.radius, 0.0f, 0.0f, 0.0f)};
        vkCmdPushConstants(cmd, m_postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uc), &uc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        attachmentToSampled(cmd, m_bloom.handle(), i);
      }
    }

    // --- composite -> swapchain -----------------------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "composite");
      vk::beginRendering(cmd, info.extent, {.color = info.targetView});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
      vk::setFlippedViewport(cmd, info.extent);
      vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositeLayout, 0, 1, &m_compositeSet, 0,
                              nullptr);
      const CompositeConstants cc = makeCompositeConstants(m_params);
      vkCmdPushConstants(cmd, m_compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cc), &cc);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      vkCmdEndRendering(cmd);
    }
  }

  void ui() override { paramsUi(m_params); }

 private:
  static VkExtent2D levelExtent(glm::ivec2 screen, uint32_t i) {
    const glm::ivec2 s = levelSize(screen, int(i));
    return {uint32_t(s.x), uint32_t(s.y)};
  }

  // GL: glTextureSubImage2D, and the driver deals with frames still reading the old LUT. Here, inside this
  // frame's command buffer: the staging buffer for this frame slot is free (its fence was waited on), and the
  // barriers order the copy after every earlier submitted read of the LUT and before this frame's composite.
  void uploadLut(VkCommandBuffer cmd, uint32_t frameInFlight) {
    vk::Buffer& staging = m_lutStaging[frameInFlight];
    staging.write(m_lutPixels.pixels.data(), m_lutPixels.sizeBytes());  // host-coherent; vkQueueSubmit publishes it

    // Old contents are fully replaced: UNDEFINED. The wait on earlier fragment-shader reads is write-after-read,
    // so an execution dependency (no access mask) is enough.
    vk::transitionImage(cmd, m_lut.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {uint32_t(m_lutPixels.width), uint32_t(m_lutPixels.height), 1};
    vkCmdCopyBufferToImage(cmd, staging.handle(), m_lut.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vk::transitionImage(cmd, m_lut.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);
    m_lutDirty = false;
  }

  void createTargets(VkExtent2D size) {
    const VkDevice device = m_ctx->device;
    VK_CHECK(vkDeviceWaitIdle(device));
    m_hdr = vk::Image(*m_ctx, {kHdrFormat, size, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, "13 hdr color"});
    m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, "13 depth"});

    // The bloom chain: one image, kLevels mips, level 0 at half resolution. (vk::Image's own view spans all
    // mips; it is unused — every pass works on a single level.)
    destroyLevelViews();
    const VkExtent2D base = levelExtent({int(size.width), int(size.height)}, 0);
    m_bloom = vk::Image(*m_ctx, {m_bloomFormat, base, kLevels,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, "13 bloom chain"});
    for (uint32_t i = 0; i < kLevels; ++i) {
      // One view per level (GL: glTextureView). A view used as an attachment must cover exactly one level,
      // and a view that is sampled must not include a level being rendered — so per-level views do both jobs.
      VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image = m_bloom.handle();
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = m_bloomFormat;
      viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};  // baseMipLevel = i, levelCount = 1
      VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_levelViews[i]));
      vk::debug::setName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_levelViews[i], fmt::format("13 bloom level {}", i));
    }

    // Descriptor sets that point at these views (no frame is in flight: we waited above).
    for (uint32_t i = 0; i < kLevels; ++i) {
      vk::writeImage(device, m_downSets[i], 0, i == 0 ? m_hdr.view() : m_levelViews[i - 1], m_linearClamp);
    }
    for (uint32_t i = 0; i + 1 < kLevels; ++i) {
      vk::writeImage(device, m_upSets[i], 0, m_levelViews[i + 1], m_linearClamp);
    }
    vk::writeImage(device, m_compositeSet, 0, m_hdr.view(), m_linearClamp);
    vk::writeImage(device, m_compositeSet, 1, m_levelViews[0], m_linearClamp);
    vk::writeImage(device, m_compositeSet, 2, m_lut.view(), m_linearClamp);
  }

  void destroyLevelViews() {
    for (VkImageView& view : m_levelViews) {
      vkDestroyImageView(m_ctx->device, view, nullptr);
      view = VK_NULL_HANDLE;
    }
  }

  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_room, m_cube;
  vk::Image m_albedo, m_hdr, m_depth, m_bloom, m_lut;
  VkFormat m_bloomFormat = VK_FORMAT_UNDEFINED;
  std::array<VkImageView, kLevels> m_levelViews{};
  VkSampler m_albedoSampler = VK_NULL_HANDLE, m_linearClamp = VK_NULL_HANDLE;

  Grading m_lutGrading;  // what the LUT image holds (or will, once m_lutDirty is recorded)
  ImageData m_lutPixels;
  bool m_lutDirty = false;
  std::array<vk::Buffer, vk::kFramesInFlight> m_lutStaging;

  std::array<vk::Buffer, vk::kFramesInFlight> m_sceneUbo;
  VkDescriptorSetLayout m_sceneSetLayout = VK_NULL_HANDLE, m_postSetLayout = VK_NULL_HANDLE,
                        m_compositeSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sceneSets, m_downSets, m_upSets;
  VkDescriptorSet m_compositeSet = VK_NULL_HANDLE;
  VkPipelineLayout m_sceneLayout = VK_NULL_HANDLE, m_postLayout = VK_NULL_HANDLE, m_compositeLayout = VK_NULL_HANDLE;
  VkPipeline m_scenePipeline = VK_NULL_HANDLE, m_downPipeline = VK_NULL_HANDLE, m_upPipeline = VK_NULL_HANDLE,
             m_compositePipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("13_bloom", BloomVK);

}  // namespace glint::bloom
