// 10_specialization_constants — Vulkan. One SPIR-V fragment module, three pipelines: each gets different
// values for `layout(constant_id = N)` through VkSpecializationInfo, and the driver compiles each pipeline
// with those values as true constants (dead branches removed). GL does the same with #defines + recompiles.

#include <array>

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

namespace glint::uber {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// The constants' values, laid out as raw bytes that VkSpecializationMapEntry points into.
struct SpecializationData {
  int32_t lightingModel;    // constant_id = 0
  float toonDesaturation;   // constant_id = 1
};
}  // namespace

class UberVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    m_colorFormat = swapchainFormat;
    const VkDevice device = ctx.device;
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    indexMesh(suzanne);
    m_mesh = vk::Mesh(ctx, suzanne, "suzanne");
    m_texture = vk::createTexture(ctx, loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    m_setLayout = vk::createSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("10 ubo {}", i));
      vk::writeBuffer(device, m_sets[i], 0, m_ubo[i].handle(), sizeof(Uniforms));
      vk::writeImage(device, m_sets[i], 1, m_texture.view(), m_sampler);
    }
    m_pipelineLayout = vk::createPipelineLayout(device, {m_setLayout});
    for (int i = 0; i < kModels; ++i) buildPipeline(i);
  }

  ~UberVK() override {
    const VkDevice device = m_ctx->device;
    for (VkPipeline pipeline : m_pipelines) vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
  }

  void update(float dt, Frame& frame) override {
    m_angle += m_params.rotationSpeed * dt;
    drawLabels(frame.framebufferSize);  // ImGui calls belong in update()/ui(): record() runs after ImGui::Render
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "10 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const float aspect = info.frame.aspect() / float(kModels);
    m_ubo[info.frameInFlight].write(makeUniforms(info.frame.camera.projection(aspect, ClipDepth::ZeroToOne),
                                                 info.frame.camera.view(), m_angle, m_params));

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    const uint32_t third = info.extent.width / kModels;
    for (int i = 0; i < kModels; ++i) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[size_t(i)]);
      // A third of the window, still flipped (negative height). GL: glViewport(third * i, 0, third, height).
      const VkViewport viewport{float(third * uint32_t(i)), float(info.extent.height), float(third),
                                -float(info.extent.height), 0.0f, 1.0f};
      vkCmdSetViewport(cmd, 0, 1, &viewport);
      const VkRect2D scissor{{int32_t(third * uint32_t(i)), 0}, {third, info.extent.height}};
      vkCmdSetScissor(cmd, 0, 1, &scissor);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                              &m_sets[info.frameInFlight], 0, nullptr);
      m_mesh.draw(cmd);
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override {
    if (paramsUi(m_params)) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));  // the old toon pipeline may be in use by a frame in flight
      buildPipeline(1);
    }
  }

 private:
  void buildPipeline(int model) {
    vkDestroyPipeline(m_ctx->device, m_pipelines[size_t(model)], nullptr);

    // Which bytes of `data` feed which constant_id. The values are baked into the pipeline: changing one
    // later means a new pipeline (-> the desaturation slider rebuilds the toon pipeline).
    const SpecializationData data{model, m_params.toonDesaturation};
    const VkSpecializationMapEntry entries[] = {
        {0, offsetof(SpecializationData, lightingModel), sizeof(int32_t)},
        {1, offsetof(SpecializationData, toonDesaturation), sizeof(float)},
    };
    VkSpecializationInfo specialization{};
    specialization.mapEntryCount = 2;
    specialization.pMapEntries = entries;
    specialization.dataSize = sizeof(data);
    specialization.pData = &data;

    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "10_specialization_constants/uber.vert.spv";
    desc.fragmentShader = "10_specialization_constants/uber.frag.spv";  // the same module for all three
    desc.fragmentSpecialization = &specialization;
    desc.vertexInput = m_mesh.vertexInput({kAttribPosition, kAttribNormal, kAttribUv});
    desc.layout = m_pipelineLayout;
    desc.colorFormat = m_colorFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = fmt::format("10 {}", kModelNames[model]);
    m_pipelines[size_t(model)] = vk::createGraphicsPipeline(m_ctx->device, desc);
  }

  Params m_params;
  float m_angle = 0.0f;
  vk::Context* m_ctx = nullptr;
  VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
  vk::Mesh m_mesh;
  vk::Image m_texture, m_depth;
  VkSampler m_sampler = VK_NULL_HANDLE;
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  std::array<VkPipeline, kModels> m_pipelines{};
};

GLINT_REGISTER_VK("10_specialization_constants", UberVK);

}  // namespace glint::uber
