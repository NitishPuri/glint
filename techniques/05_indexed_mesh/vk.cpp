// 05_indexed_mesh — Vulkan. The indexed mesh drawn twice with different uniforms, and optional blending.
//
// GL rewrites one uniform buffer between the two draws (the driver keeps each draw's copy). A VK command
// buffer only records *which* buffer a draw reads, so both draws would see the final contents. Instead:
// one buffer per frame slot holding both draws' blocks, and a *dynamic* uniform-buffer descriptor whose
// offset is picked per draw in vkCmdBindDescriptorSets (Glint_vk's dynamic_uniform_buffer sample).
// Blending is pipeline state, so "transparent" is a second pipeline.

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

namespace glint::indexed_mesh {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t kInstances = 2;
}  // namespace

class IndexedMeshVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;
    MeshData suzanne = loadObj(assetPath("suzanne.obj"));
    m_flatVertexCount = suzanne.vertexCount();
    indexMesh(suzanne);  // 2904 -> 590 vertices
    m_mesh = vk::Mesh(ctx, suzanne, "suzanne indexed");
    m_texture = vk::createTexture(ctx, loadImage(assetPath("suzanne.jpg")), true, "suzanne.jpg");
    m_sampler = vk::createSampler(device, {.name = "trilinear repeat"});

    // Dynamic offsets must be multiples of minUniformBufferOffsetAlignment (RADV: 16 bytes, NVIDIA often
    // 64 or 256). sizeof(shading::Uniforms) = 240, so each block is padded up to the next multiple.
    const VkDeviceSize alignment = ctx.properties.limits.minUniformBufferOffsetAlignment;
    m_stride = (sizeof(shading::Uniforms) + alignment - 1) / alignment * alignment;
    log::debug("05 dynamic UBO: alignment {}, stride {}", alignment, m_stride);

    m_setLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
                                               {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pool = vk::createPool(device, vk::kFramesInFlight,
                            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, vk::kFramesInFlight},
                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk::kFramesInFlight}});
    m_sets = vk::allocateSets(device, m_pool, m_setLayout, vk::kFramesInFlight);
    for (uint32_t i = 0; i < vk::kFramesInFlight; ++i) {
      m_ubo[i] = vk::Buffer(ctx, m_stride * kInstances, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            fmt::format("05 ubo {}", i));
      // range = one block; the descriptor "slides" over the buffer by the dynamic offset.
      vk::writeBuffer(device, m_sets[i], 0, m_ubo[i].handle(), sizeof(shading::Uniforms),
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
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
    desc.name = "05 opaque";
    m_opaque = vk::createGraphicsPipeline(device, desc);
    desc.alphaBlend = true;  // the only difference; GL: glEnable(GL_BLEND) + glBlendFunc
    desc.name = "05 transparent";
    m_transparent = vk::createGraphicsPipeline(device, desc);
  }

  ~IndexedMeshVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_opaque, nullptr);
    vkDestroyPipeline(device, m_transparent, nullptr);
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
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "05 depth"});
    }
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    const Camera& camera = info.frame.camera;
    const glm::mat4 projection = camera.projection(info.frame.aspect(), ClipDepth::ZeroToOne);
    vk::Buffer& ubo = m_ubo[info.frameInFlight];
    for (uint32_t instance = 0; instance < kInstances; ++instance) {
      const shading::Uniforms u = shading::makeUniforms(projection, camera.view(), model(int(instance)),
                                                        m_params.light, material(m_params, int(instance)));
      ubo.write(&u, sizeof(u), instance * m_stride);  // both blocks written *before* the GPU runs anything
    }

    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .depth = m_depth.view()});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_params.transparent ? m_transparent : m_opaque);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    for (uint32_t instance = 0; instance < kInstances; ++instance) {
      // Same set, different dynamic offset: draw 0 reads block 0, draw 1 reads block 1.
      const uint32_t offset = uint32_t(instance * m_stride);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                              &m_sets[info.frameInFlight], 1, &offset);
      m_mesh.draw(cmd);  // vkCmdDrawIndexed: 2904 indices into 590 vertices
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override {
    ImGui::Text("indexing: %zu -> %u vertices (%.0f%% fewer)", m_flatVertexCount, m_mesh.vertexCount(),
                100.0 * (1.0 - double(m_mesh.vertexCount()) / double(m_flatVertexCount)));
    paramsUi(m_params);
  }

 private:
  Params m_params;
  vk::Context* m_ctx = nullptr;
  vk::Mesh m_mesh;
  size_t m_flatVertexCount = 0;
  vk::Image m_texture, m_depth;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VkDeviceSize m_stride = 0;
  std::array<vk::Buffer, vk::kFramesInFlight> m_ubo;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_sets;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_opaque = VK_NULL_HANDLE, m_transparent = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("05_indexed_mesh", IndexedMeshVK);

}  // namespace glint::indexed_mesh
