// 06_normal_mapping — Vulkan. Four attributes (position, normal, uv, tangent), three textures: one set with
// the uniform block at binding 0 and three combined image samplers at bindings 1-3.

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

namespace glint::normal_mapping {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}

class NormalMappingVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData cylinder = loadObj(assetPath("cylinder/cylinder.obj"));
    indexMesh(cylinder);
    computeTangents(cylinder);
    m_mesh = vk::Mesh(ctx, cylinder, "cylinder");
    m_diffuse = vk::createTexture(ctx, loadImage(assetPath("cylinder/diffuse.jpg")), true, "cylinder diffuse");
    m_normal = vk::createTexture(ctx, loadImage(assetPath("cylinder/normal.jpg")), true, "cylinder normal");
    m_specular = vk::createTexture(ctx, loadImage(assetPath("cylinder/specular.jpg")), true, "cylinder specular");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    // Three separate bindings (one per texture). The alternative is a single binding with
    // descriptorCount = 3 (an array of samplers in GLSL); separate bindings read closer to GL's three units.
    constexpr VkShaderStageFlags frag = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_setLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | frag},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, frag},
                 {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, frag},
                 {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, frag}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, sizeof(shading::Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("06 ubo {}", i));
      vk::writeBuffer(device, m_sets[i], 0, m_ubo[i].handle(), sizeof(shading::Uniforms));
      vk::writeImage(device, m_sets[i], 1, m_diffuse.view(), m_sampler);  // one sampler, three images
      vk::writeImage(device, m_sets[i], 2, m_normal.view(), m_sampler);
      vk::writeImage(device, m_sets[i], 3, m_specular.view(), m_sampler);
    }
    m_pipelineLayout = vk::createPipelineLayout(device, {m_setLayout});

    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "06_normal_mapping/normal_mapping.vert.spv";
    desc.fragmentShader = "06_normal_mapping/normal_mapping.frag.spv";
    desc.vertexInput = m_mesh.vertexInput();  // all four: position, normal, uv, tangent
    desc.layout = m_pipelineLayout;
    desc.colorFormat = swapchainFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = "06 normal mapping";
    m_pipeline = vk::createGraphicsPipeline(device, desc);
  }

  ~NormalMappingVK() override {
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
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "06 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const Camera& camera = info.frame.camera;
    shading::Uniforms u = shading::makeUniforms(camera.projection(info.frame.aspect(), ClipDepth::ZeroToOne),
                                                camera.view(), glm::mat4(1.0f), m_params.light, m_params.material);
    u.material.w = m_params.useNormalMap ? 1.0f : 0.0f;
    m_ubo[info.frameInFlight].write(u);

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                            &m_sets[info.frameInFlight], 0, nullptr);
    m_mesh.draw(cmd);
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params); }

 private:
  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_diffuse, m_normal, m_specular, m_depth;
  VkSampler m_sampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("06_normal_mapping", NormalMappingVK);

}  // namespace glint::normal_mapping
