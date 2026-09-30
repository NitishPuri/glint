// 08_shadow_mapping — Vulkan. Pass 1: depth from the light into a shadow-map image (depth-only pipeline:
// no fragment shader, no color attachment). Pass 2: the camera view, comparing against the map through a
// sampler with compareEnable. Same shaders as GL apart from bindings; the differences are in the matrices,
// the barriers, and how the UI shows the map.

#include <imgui_impl_vulkan.h>

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

namespace glint::shadow_mapping {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkPipelineStageFlags2 kDepthTests =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
}  // namespace

class ShadowMappingVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData room = loadObj(assetPath("room/room_thickwalls.obj"));
    indexMesh(room);
    m_mesh = vk::Mesh(ctx, room, "room");
    m_texture = vk::createTexture(ctx, loadImage(assetPath("room/uvmap.jpg")), true, "room uvmap");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    // The comparison sampler — GL: GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE (what Glint_gl forgot).
    // Linear filtering gives the free 2x2 PCF, but for depth formats it's an optional feature: check it.
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, kDepthFormat, &props);
    const bool linear = props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!linear) log::warn("08: D32_SFLOAT can't be filtered linearly here; shadow edges will be hard");
    m_shadowSampler = vk::createSampler(device, {.filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                                 .mipmaps = false,
                                                 .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                                 .compare = true,
                                                 .border = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,  // depth 1 = lit
                                                 .name = "shadow compare"});

    // --- pass 1: depth-only pipeline; the light matrix is a push constant ------------------------------------
    m_depthLayout =
        vk::createPipelineLayout(device, {}, {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}});
    vk::GraphicsPipelineDesc depth;
    depth.vertexShader = "08_shadow_mapping/depth.vert.spv";
    // no fragmentShader, no colorFormat: VK is fine with a pipeline that only writes depth.
    depth.vertexInput = vk::Mesh::positionsOnly();
    depth.layout = m_depthLayout;
    depth.depthFormat = kDepthFormat;
    depth.name = "08 shadow pass";
    m_depthPipeline = vk::createGraphicsPipeline(device, depth);

    // --- pass 2: uniforms + diffuse + shadow map ------------------------------------------------------------
    m_setLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                 {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("08 ubo {}", i));
      vk::writeBuffer(device, m_sets[i], 0, m_ubo[i].handle(), sizeof(Uniforms));
      vk::writeImage(device, m_sets[i], 1, m_texture.view(), m_sampler);
      // binding 2 (shadow map) is written in createShadowMap()
    }
    m_shadowLayout = vk::createPipelineLayout(device, {m_setLayout});
    vk::GraphicsPipelineDesc shadow;
    shadow.vertexShader = "08_shadow_mapping/shadow.vert.spv";
    shadow.fragmentShader = "08_shadow_mapping/shadow.frag.spv";
    shadow.vertexInput = m_mesh.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    shadow.layout = m_shadowLayout;
    shadow.colorFormat = swapchainFormat;
    shadow.depthFormat = kDepthFormat;
    shadow.name = "08 camera pass";
    m_shadowPipeline = vk::createGraphicsPipeline(device, shadow);

    createShadowMap();
  }

  ~ShadowMappingVK() override {
    const VkDevice device = m_ctx->device;
    destroyShadowMapViews();
    vkDestroyPipeline(device, m_depthPipeline, nullptr);
    vkDestroyPipeline(device, m_shadowPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_depthLayout, nullptr);
    vkDestroyPipelineLayout(device, m_shadowLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
    vkDestroySampler(device, m_shadowSampler, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "08 camera depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const Camera& camera = info.frame.camera;
    // VK: clip-space z in [0, 1] for the light too.
    const glm::mat4 lightViewProj = lightViewProjection(m_params, ClipDepth::ZeroToOne);
    // Light clip space -> shadow map lookup. x: [-1,1] -> [0,1] as in GL. y: *negated*, because with the
    // flipped viewport row 0 of the shadow map is light-space +Y (the top); GL's row 0 is the bottom.
    // z: already [0,1] in VK — GL needed z * 0.5 + 0.5 as well.
    const glm::mat4 bias(0.5f, 0.0f, 0.0f, 0.0f,   //
                         0.0f, -0.5f, 0.0f, 0.0f,  //
                         0.0f, 0.0f, 1.0f, 0.0f,   //
                         0.5f, 0.5f, 0.0f, 1.0f);
    const glm::mat4 model(1.0f);
    const glm::mat4 view = camera.view();
    m_ubo[info.frameInFlight].write(
        Uniforms{camera.projection(info.frame.aspect(), ClipDepth::ZeroToOne) * view * model, view, model,
                 bias * lightViewProj, glm::vec4(glm::normalize(m_params.lightPosition), 0.0f),
                 glm::vec4(m_params.lightColor, m_params.lightPower),
                 glm::vec4(m_params.ambient, m_params.specular, m_params.bias, m_params.pcf ? 1.0f : 0.0f)});

    // --- pass 1: depth from the light --------------------------------------------------------------------
    // src: last frame's reads of the map (pass 2 and possibly the ImGui preview) — write-after-read,
    // execution dependency only.
    vk::transitionImage(cmd, m_shadowMap.handle(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        kDepthTests,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    {
      vk::debug::Label label(cmd, "pass 1: shadow map");
      vk::beginRendering(cmd, m_shadowMap.extent(), {.depth = m_shadowMap.view(), .storeDepth = true});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthPipeline);
      vk::setFlippedViewport(cmd, m_shadowMap.extent());
      vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
      const glm::mat4 lightMvp = lightViewProj * model;
      vkCmdPushConstants(cmd, m_depthLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(lightMvp), &lightMvp);
      m_mesh.drawPositionsOnly(cmd);
      vkCmdEndRendering(cmd);
    }

    // --- depth writes -> fragment shader reads (GL: implicit) ------------------------------------------------
    vk::transitionImage(cmd, m_shadowMap.handle(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kDepthTests,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT);

    // --- pass 2: camera view ------------------------------------------------------------------------------
    {
      vk::debug::Label label(cmd, "pass 2: camera");
      vk::transitionDepthForRendering(cmd, m_depth.handle());
      vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
      vk::setFlippedViewport(cmd, info.extent);
      vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowLayout, 0, 1,
                              &m_sets[info.frameInFlight], 0, nullptr);
      m_mesh.draw(cmd);
      vkCmdEndRendering(cmd);
    }
    // The map stays in SHADER_READ_ONLY_OPTIMAL: the ImGui pass after us may show it.
  }

  void ui() override {
    if (paramsUi(m_params)) createShadowMap();
    if (m_params.showShadowMap) {
      // ImGui's VK backend takes a descriptor set (image view + sampler) as ImTextureID. No v flip here: with
      // the flipped viewport row 0 is already the top of the light's view (GL's preview needed uv (0,1)-(1,0)).
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::Image(ImTextureID(m_previewTexture), ImVec2(w, w));
    }
  }

 private:
  void createShadowMap() {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));  // the old map may still be in use (or shown by ImGui)
    destroyShadowMapViews();
    const uint32_t size = uint32_t(kShadowMapSizes[m_params.shadowMapSize]);
    m_shadowMap = vk::Image(*m_ctx, {kDepthFormat, {size, size}, 1,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                     VK_IMAGE_ASPECT_DEPTH_BIT, "08 shadow map"});
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      vk::writeImage(m_ctx->device, m_sets[i], 2, m_shadowMap.view(), m_shadowSampler);
    }

    // A second view of the same image for the UI, with a component swizzle so depth shows as gray instead
    // of red. GL: GL_TEXTURE_SWIZZLE_RGBA on the texture itself. VK: swizzles belong to *views*, so the
    // shader's view and the UI's view can differ.
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_shadowMap.handle();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kDepthFormat;
    viewInfo.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                           VK_COMPONENT_SWIZZLE_ONE};
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &m_previewView));
    // ImGui wraps the view in a descriptor set with its own (non-comparing) sampler.
    m_previewTexture = ImGui_ImplVulkan_AddTexture(m_previewView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void destroyShadowMapViews() {
    if (m_previewTexture) ImGui_ImplVulkan_RemoveTexture(m_previewTexture);
    vkDestroyImageView(m_ctx->device, m_previewView, nullptr);
    m_previewTexture = VK_NULL_HANDLE;
    m_previewView = VK_NULL_HANDLE;
  }

  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_texture, m_depth, m_shadowMap;
  VkImageView m_previewView = VK_NULL_HANDLE;
  VkDescriptorSet m_previewTexture = VK_NULL_HANDLE;  // owned by ImGui's descriptor pool
  VkSampler m_sampler = VK_NULL_HANDLE, m_shadowSampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_depthLayout = VK_NULL_HANDLE, m_shadowLayout = VK_NULL_HANDLE;
  VkPipeline m_depthPipeline = VK_NULL_HANDLE, m_shadowPipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("08_shadow_mapping", ShadowMappingVK);

}  // namespace glint::shadow_mapping
