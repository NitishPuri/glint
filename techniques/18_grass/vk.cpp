// 18_grass — Vulkan. One instanced draw for the whole field: vkCmdDraw(stripVertices, bladeCount, 0, 0). What makes
// a buffer "per instance" is part of the *pipeline*: its vertex-input binding 1 has inputRate INSTANCE. The wind
// texture is made by 16's generate.comp (its SPIR-V, compiled in 16's folder) on the first frame. Shaders are
// shared with GL.

#include "core/camera.h"
#include "core/log.h"
#include "params.h"
#include "vk/barrier.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/image.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"
#include "vk/texture.h"

namespace glint::grass {

namespace {
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat kWindFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // 16's rgba16f
constexpr VkShaderStageFlags kDrawStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
}  // namespace

class GrassVK final : public vk::Technique {
 public:
  void setupCamera(Camera& camera) override { camera.setHome(kCameraHome); }

  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;

    // The wind texture: a 3D image (vk::Image grew a depth for this; 16 writes the same thing out raw).
    const int n = windGeneration().extent();
    m_wind = vk::Image(ctx, {kWindFormat, {uint32_t(n), uint32_t(n)}, 1,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                             "18 wind (16's noise)", uint32_t(n)});
    m_sampler = vk::createSampler(device, {.mipmaps = false, .address = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                           .name = "wind: trilinear repeat (3D)"});

    // Set for 16's compute shader (storage image, GENERAL) and one for drawing. The draw set is read by the
    // *vertex* shader (wind) and the ground's fragment shader: the binding's stageFlags must say both.
    m_generateSetLayout =
        vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}});
    m_drawSetLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, kDrawStages}});
    m_pool = vk::createPool(device, 2,
                            {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}});
    m_generateSet = vk::allocateSets(device, m_pool, m_generateSetLayout, 1).front();
    m_drawSet = vk::allocateSets(device, m_pool, m_drawSetLayout, 1).front();
    const VkDescriptorImageInfo storage{VK_NULL_HANDLE, m_wind.view(), VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_generateSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &storage;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    vk::writeImage(device, m_drawSet, 0, m_wind.view(), m_sampler);

    m_generateLayout = vk::createPipelineLayout(
        device, {m_generateSetLayout}, {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(noise::GenerateConstants)}});
    m_drawLayout = vk::createPipelineLayout(device, {m_drawSetLayout}, {{kDrawStages, 0, sizeof(DrawConstants)}});
    // Another technique's shader, by its SPIR-V path: 16_noise/generate.comp.spv is built because 16 has a vk.cpp.
    m_generatePipeline =
        vk::createComputePipeline(device, "16_noise/generate.comp.spv", m_generateLayout, "18 wind (16 generate)");

    // The instancing part of the pipeline: two bindings with different input rates. GL: two
    // glVertexArrayVertexBuffer bindings + glVertexArrayBindingDivisor(vao, 1, 1).
    vk::GraphicsPipelineDesc grass;
    grass.vertexShader = "18_grass/grass.vert.spv";
    grass.fragmentShader = "18_grass/grass.frag.spv";
    grass.vertexInput.bindings = {
        {0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX},  // the blade strip: next element every vertex
        {1, sizeof(Blade), VK_VERTEX_INPUT_RATE_INSTANCE},    // the blades: next element every instance
    };
    // (A divisor > 1, "advance every N instances", needs VK_KHR_vertex_attribute_divisor — core in 1.4. GL has had
    // any divisor since 3.3.)
    grass.vertexInput.attributes = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},                                  // aStrip
        {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Blade, rootAngle)},  // aRoot
        {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Blade, shape)},      // aShape
    };
    grass.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;  // GL: the draw's mode argument
    grass.layout = m_drawLayout;
    grass.colorFormat = swapchainFormat;
    grass.depthFormat = kDepthFormat;
    grass.name = "18 grass";
    m_grassPipeline = vk::createGraphicsPipeline(device, grass);

    vk::GraphicsPipelineDesc ground = grass;
    ground.vertexShader = "18_grass/ground.vert.spv";
    ground.fragmentShader = "18_grass/ground.frag.spv";
    ground.vertexInput = {};  // corners from gl_VertexIndex
    ground.name = "18 ground";
    m_groundPipeline = vk::createGraphicsPipeline(device, ground);
  }

  ~GrassVK() override {
    const VkDevice device = m_ctx->device;
    vkDestroyPipeline(device, m_generatePipeline, nullptr);
    vkDestroyPipeline(device, m_grassPipeline, nullptr);
    vkDestroyPipeline(device, m_groundPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_generateLayout, nullptr);
    vkDestroyPipelineLayout(device, m_drawLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_generateSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_drawSetLayout, nullptr);
    vkDestroySampler(device, m_sampler, nullptr);
  }

  void update(float dt, Frame& frame) override {
    m_time += dt;
    const VkExtent2D size{uint32_t(frame.framebufferSize.x), uint32_t(frame.framebufferSize.y)};
    if (size.width != m_depth.extent().width || size.height != m_depth.extent().height) {
      VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
      m_depth = vk::Image(*m_ctx, {kDepthFormat, size, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, "18 depth"});
    }
    if (!(m_built == m_params.field)) build(m_params.field);
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    if (!m_windReady) generateWind(cmd);

    const DrawConstants dc =
        makeDrawConstants(m_params, info.frame.camera.viewProjection(info.frame.aspect(), ClipDepth::ZeroToOne),
                          info.frame.camera.eye, m_time, int(m_bladeCount));
    vk::transitionDepthForRendering(cmd, m_depth.handle());
    vk::beginRendering(cmd, info.extent, {.color = info.targetView,
                                          .clearColor = {0.62f, 0.74f, 0.86f, 1.0f},  // = GL's (UNORM swapchain)
                                          .depth = m_depth.view()});
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);  // blades are seen from both sides
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawLayout, 0, 1, &m_drawSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_drawLayout, kDrawStages, 0, sizeof(dc), &dc);

    {
      vk::debug::Label label(cmd, "ground");
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_groundPipeline);
      vkCmdDraw(cmd, 4, 1, 0, 0);
    }
    {
      // Push constants and descriptor sets stay bound across vkCmdBindPipeline: both pipelines use m_drawLayout.
      vk::debug::Label label(cmd, "grass (instanced)");
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_grassPipeline);
      const VkBuffer buffers[] = {m_strip.handle(), m_blades.handle()};
      const VkDeviceSize offsets[] = {0, 0};
      vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);  // bindings 0 and 1; their rates are in the pipeline
      vkCmdDraw(cmd, m_stripVertices, m_bladeCount, 0, 0);  // vertexCount, instanceCount, firstVertex, firstInstance
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params); }

 private:
  // 16's compute shader into the 3D image, recorded once (as 16's generate()): UNDEFINED → GENERAL, dispatch,
  // GENERAL → SHADER_READ_ONLY for sampling — by the *vertex* shader too, so that stage goes in the barrier.
  void generateWind(VkCommandBuffer cmd) {
    vk::debug::Label label(cmd, "wind (16's generate.comp)");
    const noise::Generation gen = windGeneration();
    vk::transitionImage(cmd, m_wind.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_generatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_generateLayout, 0, 1, &m_generateSet, 0, nullptr);
    const noise::GenerateConstants gc = noise::makeGenerateConstants(gen);
    vkCmdPushConstants(cmd, m_generateLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gc), &gc);
    const glm::uvec3 groups = noise::groupCounts(gen);
    vkCmdDispatch(cmd, groups.x, groups.y, groups.z);
    vk::transitionImage(cmd, m_wind.handle(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    m_windReady = true;
    log::info("18_grass: wind texture {}^3 from 16_noise/generate.comp", gen.extent());
  }

  void build(const Field& field) {
    const std::vector<glm::vec2> strip = bladeStrip(field.segments);
    const std::vector<Blade> blades = generateBlades(field);
    // The old buffers may still be read by frames in flight: wait before replacing them (GL does this for us).
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));
    m_strip = vk::createDeviceLocalBuffer(*m_ctx, strip.data(), strip.size() * sizeof(glm::vec2),
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "18 blade strip");
    // An instance buffer is an ordinary VERTEX_BUFFER: the rate isn't a property of the buffer.
    m_blades = vk::createDeviceLocalBuffer(*m_ctx, blades.data(), blades.size() * sizeof(Blade),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "18 blades (instances)");
    m_stripVertices = uint32_t(strip.size());
    m_bladeCount = uint32_t(blades.size());
    m_built = field;
    log::info("18_grass: {} blades x {} vertices, instance buffer {:.1f} MB", m_bladeCount, m_stripVertices,
              double(blades.size() * sizeof(Blade)) / (1 << 20));
  }

  Params m_params;
  Field m_built{.size = 0};  // what the buffers hold (size 0: nothing yet)
  float m_time = 0.0f;
  vk::Context* m_ctx = nullptr;
  vk::Image m_wind, m_depth;
  bool m_windReady = false;
  VkSampler m_sampler = VK_NULL_HANDLE;
  vk::Buffer m_strip, m_blades;
  uint32_t m_stripVertices = 0, m_bladeCount = 0;
  VkDescriptorSetLayout m_generateSetLayout = VK_NULL_HANDLE, m_drawSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  VkDescriptorSet m_generateSet = VK_NULL_HANDLE, m_drawSet = VK_NULL_HANDLE;
  VkPipelineLayout m_generateLayout = VK_NULL_HANDLE, m_drawLayout = VK_NULL_HANDLE;
  VkPipeline m_generatePipeline = VK_NULL_HANDLE, m_grassPipeline = VK_NULL_HANDLE, m_groundPipeline = VK_NULL_HANDLE;
};

GLINT_REGISTER_VK("18_grass", GrassVK);

}  // namespace glint::grass
