// 07_render_to_texture — Vulkan. Pass 1: the lit mesh into offscreen color + depth images. Pass 2: a
// full-screen triangle reading them. What GL's FBO + implicit synchronisation hide is spelled out here:
//   - no framebuffer object: pass 1's attachments are just listed in vkCmdBeginRendering
//   - a barrier between the passes (attachment writes -> fragment shader reads, plus layout change)
//   - a barrier before pass 1 as well: both frames in flight share the offscreen images, so this frame's
//     pass 1 must not overwrite them while the previous frame's pass 2 may still be reading them

#include "../common/shading.h"
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

namespace glint::render_to_texture {

namespace {
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;  // GL: GL_RGBA8
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;      // GL: GL_DEPTH_COMPONENT32F
}  // namespace

class RenderToTextureVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    indexMesh(suzanne);
    m_mesh = vk::Mesh(ctx, suzanne, "suzanne");
    m_texture = vk::createTexture(ctx, loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_meshSampler = vk::createSampler(device, {.name = "trilinear repeat"});
    m_colorSampler = vk::createSampler(
        device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .name = "linear clamp"});
    // Depth for display only: nearest is enough and needs no format support check (linear filtering of
    // depth formats is optional in VK).
    m_depthSampler = vk::createSampler(device, {.filter = VK_FILTER_NEAREST, .mipmaps = false,
                                                .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                .name = "nearest clamp"});

    // --- pass 1: the standard shading pipeline, rendering into RGBA8 + D32 (not the swapchain format) ------
    m_sceneSetLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    // --- pass 2: post uniforms + offscreen color + offscreen depth ------------------------------------------
    m_postSetLayout =
        vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                                     {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                                     {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, 2 * vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * vk::kFramesInFlight}});
    m_sceneSets = vk::allocateSets(device, m_pool, m_sceneSetLayout, vk::kFramesInFlight);
    m_postSets = vk::allocateSets(device, m_pool, m_postSetLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      constexpr VkMemoryPropertyFlags hostMemory =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      m_sceneUbo[i] = vk::Buffer(ctx, sizeof(shading::Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostMemory,
                                 fmt::format("07 scene ubo {}", i));
      m_postUbo[i] = vk::Buffer(ctx, sizeof(PostUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostMemory,
                                fmt::format("07 post ubo {}", i));
      vk::writeBuffer(device, m_sceneSets[i], 0, m_sceneUbo[i].handle(), sizeof(shading::Uniforms));
      vk::writeImage(device, m_sceneSets[i], 1, m_texture.view(), m_meshSampler);
      vk::writeBuffer(device, m_postSets[i], 0, m_postUbo[i].handle(), sizeof(PostUniforms));
      // Bindings 1-2 point at the offscreen images: written in createTargets(), which runs on every resize.
    }
    m_sceneLayout = vk::createPipelineLayout(device, {m_sceneSetLayout});
    m_postLayout = vk::createPipelineLayout(device, {m_postSetLayout});

    vk::GraphicsPipelineDesc scene;
    scene.vertexShader = "common/shading.vert.spv";
    scene.fragmentShader = "common/shading.frag.spv";
    scene.vertexInput = m_mesh.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    scene.layout = m_sceneLayout;
    scene.colorFormat = kColorFormat;
    scene.depthFormat = kDepthFormat;
    scene.name = "07 scene (offscreen)";
    m_scenePipeline = vk::createGraphicsPipeline(device, scene);

    vk::GraphicsPipelineDesc post;
    post.vertexShader = "07_render_to_texture/fullscreen.vert.spv";
    post.fragmentShader = "07_render_to_texture/post.frag.spv";
    // vertexInput left empty: the triangle comes from gl_VertexIndex (GL still needed an empty VAO for this).
    post.layout = m_postLayout;
    post.colorFormat = swapchainFormat;
    post.name = "07 post";
    m_postPipeline = vk::createGraphicsPipeline(device, post);
  }

  ~RenderToTextureVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_scenePipeline, nullptr);
    vkDestroyPipeline(device, m_postPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_sceneLayout, nullptr);
    vkDestroyPipelineLayout(device, m_postLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_sceneSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_postSetLayout, nullptr);
    vkDestroySampler(device, m_meshSampler, nullptr);
    vkDestroySampler(device, m_colorSampler, nullptr);
    vkDestroySampler(device, m_depthSampler, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_color.extent().width || size.height != m_color.extent().height) createTargets(size);
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const uint32_t f = info.frameInFlight;
    const Frame& frame = info.frame;
    m_sceneUbo[f].write(shading::makeUniforms(frame.camera.projection(frame.aspect(), ClipDepth::ZeroToOne),
                                              frame.camera.view(), glm::mat4(1.0f), m_params.light,
                                              m_params.material));
    m_postUbo[f].write(PostUniforms{
        glm::vec4(float(frame.time), m_params.wobble, 0.0f, 0.0f),
        glm::vec4(frame.camera.nearZ, frame.camera.farZ, m_params.depthRange, m_params.showDepth ? 1.0f : 0.0f)});

    constexpr VkPipelineStageFlags2 depthTests =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

    // --- before pass 1: offscreen images -> attachments ------------------------------------------------------
    // src = the previous frame's pass 2, which sampled them in the fragment shader. A write-after-read only
    // needs an execution dependency, hence srcAccess = 0. UNDEFINED: we clear them anyway.
    vk::transitionImage(cmd, m_color.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    vk::transitionImage(cmd, m_depth.handle(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        depthTests,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // --- pass 1: scene -> offscreen ----------------------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "pass 1: scene to texture");
      // storeDepth: pass 2 reads the depth, so it must actually be written back (GL: always is).
      vk::beginRendering(cmd, m_color.extent(),
                         {.color = m_color.view(), .depth = m_depth.view(), .storeDepth = true});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_scenePipeline);
      vk::setFlippedViewport(cmd, m_color.extent());
      vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &m_sceneSets[f], 0,
                              nullptr);
      m_mesh.draw(cmd);
      vkCmdEndRendering(cmd);
    }

    // --- between the passes: attachment writes -> fragment shader reads ----------------------------------------
    // This is the barrier GL inserts for you when you sample a texture you just rendered into.
    // (dstAccess SHADER_READ rather than SHADER_SAMPLED_READ: see the convention note in vk/barrier.h.)
    vk::transitionImage(cmd, m_color.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);
    vk::transitionImage(cmd, m_depth.handle(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthTests,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);

    // --- pass 2: offscreen -> swapchain ------------------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "pass 2: post-process");
      vk::beginRendering(cmd, info.extent, {.color = info.targetView});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeline);
      vk::setFlippedViewport(cmd, info.extent);
      vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postLayout, 0, 1, &m_postSets[f], 0, nullptr);
      vkCmdDraw(cmd, 3, 1, 0, 0);  // GL: glDrawArrays(GL_TRIANGLES, 0, 3) with an empty VAO bound
      vkCmdEndRendering(cmd);
    }
  }

  void ui() override { paramsUi(m_params); }

 private:
  void createTargets(VkExtent2D size) {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));  // frames in flight may still use the old images
    // Attachment *and* sampled: pass 1 renders into them, pass 2 reads them.
    m_color = vk::Image(*m_ctx, {kColorFormat, size, 1,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, "07 offscreen color"});
    m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1,
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, "07 offscreen depth"});
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      vk::writeImage(m_ctx->device, m_postSets[i], 1, m_color.view(), m_colorSampler);
      vk::writeImage(m_ctx->device, m_postSets[i], 2, m_depth.view(), m_depthSampler);
    }
  }

  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_texture;
  vk::Image m_color, m_depth;  // the offscreen targets
  VkSampler m_meshSampler = VK_NULL_HANDLE, m_colorSampler = VK_NULL_HANDLE, m_depthSampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_sceneUbo, m_postUbo;
  VkDescriptorSetLayout m_sceneSetLayout = VK_NULL_HANDLE, m_postSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sceneSets, m_postSets;
  VkPipelineLayout m_sceneLayout = VK_NULL_HANDLE, m_postLayout = VK_NULL_HANDLE;
  VkPipeline m_scenePipeline = VK_NULL_HANDLE, m_postPipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("07_render_to_texture", RenderToTextureVK);

}  // namespace glint::render_to_texture
