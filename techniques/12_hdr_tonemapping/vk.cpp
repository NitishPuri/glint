// 12_hdr_tonemapping — Vulkan. Pass 1: the lit room into an R16G16B16A16_SFLOAT image (linear, unclamped).
// Pass 2: a full-screen triangle applies exposure + tone mapping + sRGB encoding into the swapchain image.
// Same shader sources as GL (techniques/common/shaders/glint.glsl); differences live here.

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

namespace glint::hdr {

namespace {
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // GL: GL_RGBA16F
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkShaderStageFlags kSceneStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
}  // namespace

class HdrVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_room = vk::Mesh(ctx, room, "room");
    m_cube = vk::Mesh(ctx, makeCube(), "light marker");
    // VK_FORMAT_R8G8B8A8_SRGB: sampling decodes to linear, and createTexture's mip blits filter in linear
    // space too (blits between sRGB images convert). GL: GL_SRGB8_ALPHA8.
    m_albedo = vk::createTexture(ctx, loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap (sRGB)", /*srgb*/ true);
    m_albedoSampler = vk::createSampler(device, {.name = "trilinear repeat"});
    m_hdrSampler = vk::createSampler(
        device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .name = "linear clamp"});

    // --- pass 1: scene set (UBO + albedo) + per-draw push constants -----------------------------------------
    m_sceneSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kSceneStages},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    // --- pass 2: the HDR image + push constants -------------------------------------------------------------
    m_tonemapSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight + 1,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight + 1}});
    m_sceneSets = vk::allocateSets(device, m_pool, m_sceneSetLayout, vk::kFramesInFlight);
    m_tonemapSet = vk::allocateSets(device, m_pool, m_tonemapSetLayout, 1).front();
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_sceneUbo[i] = vk::Buffer(ctx, sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 fmt::format("12 scene {}", i));
      vk::writeBuffer(device, m_sceneSets[i], 0, m_sceneUbo[i].handle(), sizeof(SceneUniforms));
      vk::writeImage(device, m_sceneSets[i], 1, m_albedo.view(), m_albedoSampler);
    }
    m_sceneLayout = vk::createPipelineLayout(device, {m_sceneSetLayout}, {{kSceneStages, 0, sizeof(DrawConstants)}});
    m_tonemapLayout = vk::createPipelineLayout(device, {m_tonemapSetLayout},
                                               {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TonemapConstants)}});

    vk::GraphicsPipelineDesc scene;
    scene.vertexShader = "12_hdr_tonemapping/scene.vert.spv";
    scene.fragmentShader = "12_hdr_tonemapping/scene.frag.spv";
    scene.vertexInput = m_room.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    scene.layout = m_sceneLayout;
    scene.colorFormat = kHdrFormat;  // renders into the float image, not the swapchain
    scene.depthFormat = kDepthFormat;
    scene.name = "12 scene (HDR)";
    m_scenePipeline = vk::createGraphicsPipeline(device, scene);

    vk::GraphicsPipelineDesc tonemap;
    tonemap.vertexShader = "common/fullscreen.vert.spv";
    tonemap.fragmentShader = "12_hdr_tonemapping/tonemap.frag.spv";
    tonemap.layout = m_tonemapLayout;
    tonemap.colorFormat = swapchainFormat;
    tonemap.name = "12 tonemap";
    m_tonemapPipeline = vk::createGraphicsPipeline(device, tonemap);
  }

  ~HdrVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_scenePipeline, nullptr);
    vkDestroyPipeline(device, m_tonemapPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_sceneLayout, nullptr);
    vkDestroyPipelineLayout(device, m_tonemapLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_sceneSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_tonemapSetLayout, nullptr);
    vkDestroySampler(device, m_albedoSampler, nullptr);
    vkDestroySampler(device, m_hdrSampler, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_hdr.extent().width || size.height != m_hdr.extent().height) createTargets(size);
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    m_sceneUbo[info.frameInFlight].write(makeSceneUniforms(
        info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne), info.frame.camera.eye, m_params));

    // HDR image -> attachment, after the previous frame's tone-map pass read it (write-after-read).
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
      const DrawConstants roomDraw{glm::mat4(1.0f), glm::vec4(0.0f)};
      vkCmdPushConstants(cmd, m_sceneLayout, kSceneStages, 0, sizeof(roomDraw), &roomDraw);
      m_room.draw(cmd);
      for (int i = 0; i < kLights; ++i) {
        const DrawConstants marker = lightMarker(m_params, i);
        vkCmdPushConstants(cmd, m_sceneLayout, kSceneStages, 0, sizeof(marker), &marker);
        m_cube.draw(cmd);
      }
      vkCmdEndRendering(cmd);
    }

    // HDR writes -> fragment shader reads (GL does this for you).
    vk::transitionImage(cmd, m_hdr.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);
    {
      vk::debug::Label label(cmd, "pass 2: tone map");
      vk::beginRendering(cmd, info.extent, {.color = info.targetView});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline);
      vk::setFlippedViewport(cmd, info.extent);
      vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapLayout, 0, 1, &m_tonemapSet, 0, nullptr);
      const TonemapConstants tc = makeTonemapConstants(m_params);
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
                               VK_IMAGE_ASPECT_COLOR_BIT, "12 hdr color"});
    m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, "12 depth"});
    vk::writeImage(m_ctx->device, m_tonemapSet, 0, m_hdr.view(), m_hdrSampler);
  }

  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_room, m_cube;
  vk::Image m_albedo, m_hdr, m_depth;
  VkSampler m_albedoSampler = VK_NULL_HANDLE, m_hdrSampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_sceneUbo;
  VkDescriptorSetLayout m_sceneSetLayout = VK_NULL_HANDLE, m_tonemapSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sceneSets;
  VkDescriptorSet m_tonemapSet = VK_NULL_HANDLE;
  VkPipelineLayout m_sceneLayout = VK_NULL_HANDLE, m_tonemapLayout = VK_NULL_HANDLE;
  VkPipeline m_scenePipeline = VK_NULL_HANDLE, m_tonemapPipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("12_hdr_tonemapping", HdrVK);

}  // namespace glint::hdr
