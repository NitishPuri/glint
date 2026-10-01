// 11_gltf — Vulkan. Data is bound at three frequencies:
//   set 0 (per frame slot): the frame UBO — bound once per frame
//   set 1 (per material):   base color + normal texture — one set per material, written once at load,
//                           bound when the material changes
//   push constants:         the draw's model matrix and material factors — every draw
// GL does the same with a UBO binding point, texture units and plain uniforms.

#include <vector>

#include "core/assets.h"
#include "core/camera.h"
#include "core/gltf.h"
#include "core/log.h"
#include "core/timer.h"
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

namespace glint::gltf_scene {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkShaderStageFlags kDrawStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
}  // namespace

class GltfVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    m_model = loadGltf(assetPath(kModelPath));
    fillMissingAttributes(m_model);
    for (size_t i = 0; i < m_model.primitives.size(); ++i) {
      m_meshes.emplace_back(ctx, m_model.primitives[i].mesh, fmt::format("gltf primitive {}", i));
    }
    m_textures.resize(m_model.images.size());
    {
      ScopedTimer timer("11 VK texture uploads");
      for (size_t i = 0; i < m_model.images.size(); ++i) {
        if (m_model.images[i].width > 0) {
          m_textures[i] = vk::createTexture(ctx, m_model.images[i], true, fmt::format("gltf image {}", i));
        }
      }
    }
    m_white = vk::createTexture(ctx, {1, 1, 4, {255, 255, 255, 255}}, false, "white");
    m_flatNormal = vk::createTexture(ctx, {1, 1, 4, {128, 128, 255, 255}}, false, "flat normal");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    // --- set layouts: one per update frequency ------------------------------------------------------------
    m_frameLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kDrawStages}});
    m_materialLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    // +1 material: the default one for primitives without a material.
    const uint32_t materials = uint32_t(m_model.materials.size()) + 1;
    m_pool = vk::createPool(device, vk::kFramesInFlight + materials,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * materials}});
    m_frameSets = vk::allocateSets(device, m_pool, m_frameLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_frameUbo[i] = vk::Buffer(ctx, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 fmt::format("11 frame {}", i));
      vk::writeBuffer(device, m_frameSets[i], 0, m_frameUbo[i].handle(), sizeof(FrameUniforms));
    }
    // Material sets never change after this: textures are immutable, so no per-frame copies are needed.
    m_materialSets = vk::allocateSets(device, m_pool, m_materialLayout, materials);
    for (uint32_t m = 0; m < materials; ++m) {
      const GltfMaterial mat = m < m_model.materials.size() ? m_model.materials[m] : GltfMaterial{};
      vk::writeImage(device, m_materialSets[m], 0, textureOr(mat.baseColorImage, m_white).view(), m_sampler);
      vk::writeImage(device, m_materialSets[m], 1, textureOr(mat.normalImage, m_flatNormal).view(), m_sampler);
    }

    // Two set layouts (set 0, set 1) + one push constant range used by both stages.
    m_pipelineLayout = vk::createPipelineLayout(device, {m_frameLayout, m_materialLayout},
                                                {{kDrawStages, 0, sizeof(DrawConstants)}});
    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "11_gltf/model.vert.spv";
    desc.fragmentShader = "11_gltf/model.frag.spv";
    desc.vertexInput = m_meshes.front().vertexInput({kAttribPosition, kAttribNormal, kAttribUv, kAttribTangent});
    desc.layout = m_pipelineLayout;
    desc.colorFormat = swapchainFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = "11 gltf";
    m_pipeline = vk::createGraphicsPipeline(device, desc);
  }

  ~GltfVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_frameLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_materialLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
  }

  void setupCamera(Camera& camera) override { frameModel(camera, m_model); }

  void update(float /*dt*/, Frame& frame) override {
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "11 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    m_frameUbo[info.frameInFlight].write(
        makeFrameUniforms(info.frame.camera, info.frame.aspect(), ClipDepth::ZeroToOne, m_params));

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vk::setFlippedViewport(cmd, info.extent);
    // Set 0 once per frame. Binding set 1 later doesn't disturb it: sets are independent slots.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                            &m_frameSets[info.frameInFlight], 0, nullptr);

    int boundMaterial = -2;
    for (const GltfDraw& draw : m_model.draws) {
      const int material = m_model.primitives[size_t(draw.primitive)].material;
      if (material != boundMaterial) {
        const uint32_t set = material >= 0 ? uint32_t(material) : uint32_t(m_model.materials.size());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &m_materialSets[set], 0,
                                nullptr);
        const bool doubleSided = material >= 0 && m_model.materials[size_t(material)].doubleSided;
        vkCmdSetCullMode(cmd, doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);  // dynamic state
        boundMaterial = material;
      }
      const DrawConstants dc = makeDrawConstants(m_model, draw);
      vkCmdPushConstants(cmd, m_pipelineLayout, kDrawStages, 0, sizeof(dc), &dc);
      m_meshes[size_t(draw.primitive)].draw(cmd);
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params, m_model); }

 private:
  const vk::Image& textureOr(int image, const vk::Image& fallback) const {
    return image >= 0 && m_textures[size_t(image)] ? m_textures[size_t(image)] : fallback;
  }

  Params m_params;
  ModelData m_model;
  vk::Context* m_ctx = nullptr;
  std::vector<vk::Mesh> m_meshes;
  std::vector<vk::Image> m_textures;
  vk::Image m_white, m_flatNormal, m_depth;
  VkSampler m_sampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_frameUbo;
  VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE, m_materialLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_frameSets, m_materialSets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("11_gltf", GltfVK);

}  // namespace glint::gltf_scene
