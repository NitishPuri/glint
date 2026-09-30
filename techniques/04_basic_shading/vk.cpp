// 04_basic_shading — Vulkan. The same std140 block as GL (shading::Uniforms), one copy per frame in flight,
// plus the diffuse texture, in one descriptor set. Non-indexed draw (vkCmdDraw), like GL's glDrawArrays.

#include "../common/shading.h"
#include "core/assets.h"
#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/buffer.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/mesh.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::basic_shading {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}

class BasicShadingVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    const MeshData suzanne = loadObj(assetPath("suzanne.obj"));  // flat: 2904 vertices
    m_mesh = vk::Mesh(ctx, suzanne, "suzanne");
    m_texture = vk::createTexture(ctx, loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    // Set 0: binding 0 = the Shading block (both stages read it), binding 1 = diffuse texture.
    m_setLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, sizeof(shading::Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("04 ubo {}", i));
      vk::writeBuffer(device, m_sets[i], 0, m_ubo[i].handle(), sizeof(shading::Uniforms));
      vk::writeImage(device, m_sets[i], 1, m_texture.view(), m_sampler);
    }
    m_pipelineLayout = vk::createPipelineLayout(device, {m_setLayout});

    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "common/shading.vert.spv";
    desc.fragmentShader = "common/shading.frag.spv";
    desc.vertexInput = m_mesh.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    desc.layout = m_pipelineLayout;
    desc.colorFormat = swapchainFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = "04 shading pipeline";
    m_pipeline = vk::createGraphicsPipeline(device, desc);
  }

  ~BasicShadingVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
  }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "04 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    // Same struct GL uploads with glNamedBufferSubData — only the projection differs (z in [0, 1]).
    const Camera& camera = info.frame.camera;
    m_ubo[info.frameInFlight].write(shading::makeUniforms(camera.projection(info.frame.aspect(), ClipDepth::ZeroToOne),
                                                          camera.view(), glm::mat4(1.0f), m_params.light,
                                                          m_params.material));

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                            &m_sets[info.frameInFlight], 0, nullptr);
    m_mesh.drawNonIndexed(cmd);  // vkCmdDraw(2904): three vertices per triangle, no index buffer
    vkCmdEndRendering(cmd);
  }

  void ui() override {
    ImGui::Text("%u vertices, drawn without an index buffer", m_mesh.vertexCount());
    paramsUi(m_params);
  }

 private:
  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_texture, m_depth;
  VkSampler m_sampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("04_basic_shading", BasicShadingVK);

}  // namespace glint::basic_shading
