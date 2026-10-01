// 09_dynamic_uniform_buffer — Vulkan. 125 cubes; one buffer per frame in flight holds every model matrix
// at multiples of minUniformBufferOffsetAlignment, and a UNIFORM_BUFFER_DYNAMIC descriptor is bound with a
// different offset for each draw. (GL: glBindBufferRange per draw.)

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

namespace glint::dynamic_ubo {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}

class DynamicUboVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    m_mesh = vk::Mesh(ctx, makeCube(), "cube");

    // Same idea as GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, queried from the physical device's limits.
    m_alignment = size_t(ctx.properties.limits.minUniformBufferOffsetAlignment);
    m_stride = alignUp(sizeof(glm::mat4), m_alignment);
    log::info("09 VK: minUniformBufferOffsetAlignment = {}, stride {}", m_alignment, m_stride);

    m_setLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
                                               {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                                VK_SHADER_STAGE_VERTEX_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    constexpr VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_cameraUbo[i] = vk::Buffer(ctx, sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                                  fmt::format("09 camera {}", i));
      m_objectUbo[i] = vk::Buffer(ctx, m_stride * kObjects, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                                  fmt::format("09 objects {}", i));
      vk::writeBuffer(device, m_sets[i], 0, m_cameraUbo[i].handle(), sizeof(glm::mat4));
      // range = one matrix: the window the dynamic offset slides across the buffer.
      vk::writeBuffer(device, m_sets[i], 1, m_objectUbo[i].handle(), sizeof(glm::mat4),
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    }
    m_pipelineLayout = vk::createPipelineLayout(device, {m_setLayout});

    vk::GraphicsPipelineDesc desc;
    desc.vertexShader = "09_dynamic_uniform_buffer/objects.vert.spv";
    desc.fragmentShader = "09_dynamic_uniform_buffer/objects.frag.spv";
    desc.vertexInput = m_mesh.vertexInput({kAttribPosition});
    desc.layout = m_pipelineLayout;
    desc.colorFormat = swapchainFormat;
    desc.depthFormat = kDepthFormat;
    desc.name = "09 objects";
    m_pipeline = vk::createGraphicsPipeline(device, desc);
  }

  ~DynamicUboVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
  }

  void update(float dt, Frame& frame) override {
    m_objects.animate(dt, m_params);
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "09 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const uint32_t f = info.frameInFlight;
    m_cameraUbo[f].write(info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne));
    // Straight into the mapped buffer at i * stride — no CPU-side copy needed (GL builds one and uploads it).
    for (int i = 0; i < kObjects; ++i) {
      const glm::mat4 model = m_objects.model(i);
      m_objectUbo[f].write(&model, sizeof(model), VkDeviceSize(i) * m_stride);
    }

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    for (int i = 0; i < kObjects; ++i) {
      // One dynamic offset per UNIFORM_BUFFER_DYNAMIC binding in the set, in binding order.
      const uint32_t offset = uint32_t(size_t(i) * m_stride);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_sets[f], 1, &offset);
      m_mesh.draw(cmd);
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override {
    ImGui::Text("%d objects, 1 buffer: alignment %zu, stride %zu bytes", kObjects, m_alignment, m_stride);
    paramsUi(m_params);
  }

 private:
  Params m_params;
  Objects m_objects;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  vk::Image m_depth;
  size_t m_alignment = 0, m_stride = 0;
  std::array<vk::Buffer, vk::kFramesInFlight> m_cameraUbo, m_objectUbo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("09_dynamic_uniform_buffer", DynamicUboVK);

}  // namespace glint::dynamic_ubo
