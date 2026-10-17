// 15_prefix_sum — Vulkan. Stream compaction with a multi-level scan, all compute:
//   flags -> scan level 0, 1, ... (512-element blocks) -> add block offsets back down -> scatter -> draw
// Between compute passes: a *global* memory barrier (VkMemoryBarrier2: all buffers, like glMemoryBarrier),
// where 14 used a per-buffer one. Timestamps between the phases go into a query pool of our own.
// Shaders are shared with GL.

#include <array>
#include <cstring>

#include "core/log.h"
#include "params.h"
#include "vk/buffer.h"
#include "vk/debug.h"
#include "vk/descriptors.h"
#include "vk/frame.h"
#include "vk/pipeline.h"
#include "vk/rendering.h"
#include "vk/technique.h"

namespace glint::prefix_sum {

namespace {
constexpr uint32_t kTimestamps = 4;  // per frame slot: before flags, after flags, after scan, after scatter

uint32_t groups(uint32_t n) { return (n + kThreads - 1) / kThreads; }

// A global memory barrier: covers every buffer and image. GL's glMemoryBarrier is always this kind (it can't
// name a buffer); VK lets you narrow it to one buffer range (14) or widen it like this. Drivers mostly treat
// both the same — the stages are what costs.
void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

// Compute pass N wrote through storage buffers, compute pass N+1 reads (and writes) them.
void computeToCompute(VkCommandBuffer cmd) {
  memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}
}  // namespace

class PrefixSumVK final : public vk::Technique {
 public:
  void init(vk::Context& ctx, VkFormat swapchainFormat) override {
    m_ctx = &ctx;
    const VkDevice device = ctx.device;

    // Subgroup support is a device *property* (core 1.1, nothing to enable): the size, the stages that may use
    // subgroup operations, and which operation classes exist. GL: GL_KHR_shader_subgroup's glGetIntegerv queries.
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(ctx.physicalDevice, &properties);
    m_subgroupSize = subgroup.subgroupSize;
    const bool subgroupScan = (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                              (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);
    log::info("15_prefix_sum: subgroup size {}, stages 0x{:x}, operations 0x{:x}", m_subgroupSize,
              subgroup.supportedStages, subgroup.supportedOperations);
    if (!subgroupScan) {
      log::warn("15_prefix_sum: no subgroup arithmetic in compute shaders; using the shared-memory kernel only");
      m_params.kernel = 0;
    }

    // --- buffers: storage, plus TRANSFER_SRC so the CPU check can copy them out ------------------------------
    constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    m_scan = vk::Buffer(ctx, scanBufferElements(kMaxPoints) * sizeof(uint32_t), usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "15 scan levels");
    m_flags = vk::Buffer(ctx, kMaxPoints * sizeof(uint32_t), usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "15 flags");
    m_compact = vk::Buffer(ctx, (kMaxPoints + 1) * sizeof(uint32_t), usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           "15 compact");

    // --- one descriptor set with all three, visible to compute and to the draw's vertex shader ---------------
    constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    m_setLayout = vk::createSetLayout(device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages},
                                               {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages},
                                               {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages}});
    m_pool = vk::createPool(device, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}});
    m_set = vk::allocateSets(device, m_pool, m_setLayout, 1).front();
    vk::writeBuffer(device, m_set, 0, m_scan.handle(), m_scan.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    vk::writeBuffer(device, m_set, 1, m_flags.handle(), m_flags.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    vk::writeBuffer(device, m_set, 2, m_compact.handle(), m_compact.size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    m_computeLayout = vk::createPipelineLayout(device, {m_setLayout},
                                               {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PassConstants)}});
    m_drawLayout =
        vk::createPipelineLayout(device, {m_setLayout}, {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PassConstants)}});

    // Compute pipelines through the helper now (14 wrote one out raw).
    const std::string dir = "15_prefix_sum/";
    m_flagsPipeline = vk::createComputePipeline(device, dir + "flags.comp.spv", m_computeLayout, "15 flags");
    m_scanShared = vk::createComputePipeline(device, dir + "scan_shared.comp.spv", m_computeLayout, "15 scan (shared)");
    if (subgroupScan) {
      m_scanSubgroup =
          vk::createComputePipeline(device, dir + "scan_subgroup.comp.spv", m_computeLayout, "15 scan (subgroup)");
    }
    m_addPipeline = vk::createComputePipeline(device, dir + "add_offsets.comp.spv", m_computeLayout, "15 add offsets");
    m_scatterPipeline = vk::createComputePipeline(device, dir + "scatter.comp.spv", m_computeLayout, "15 scatter");

    vk::GraphicsPipelineDesc points;
    points.vertexShader = dir + "points.vert.spv";
    points.fragmentShader = dir + "points.frag.spv";
    points.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    points.layout = m_drawLayout;
    points.colorFormat = swapchainFormat;
    points.name = "15 points";
    m_pointsPipeline = vk::createGraphicsPipeline(device, points);

    // --- timestamps: kTimestamps per frame slot -----------------------------------------------------------------
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = kTimestamps * vk::kFramesInFlight;
    VK_CHECK(vkCreateQueryPool(device, &queryInfo, nullptr, &m_queries));
    vk::debug::setName(device, VK_OBJECT_TYPE_QUERY_POOL, m_queries, "15 phase timestamps");
  }

  ~PrefixSumVK() override {
    log::info("15_prefix_sum timing: {}", m_timingLog.summary(m_params));
    const VkDevice device = m_ctx->device;
    for (VkPipeline p : {m_flagsPipeline, m_scanShared, m_scanSubgroup, m_addPipeline, m_scatterPipeline,
                         m_pointsPipeline}) {
      vkDestroyPipeline(device, p, nullptr);
    }
    vkDestroyPipelineLayout(device, m_computeLayout, nullptr);
    vkDestroyPipelineLayout(device, m_drawLayout, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    vkDestroyQueryPool(device, m_queries, nullptr);
  }

  void update(float dt, Frame& /*frame*/) override {
    if (m_readbackPending) finishVerify();  // copies recorded last frame
    if (!m_params.paused) m_time += dt;
    m_autoVerify.step(m_params, m_scanSubgroup != VK_NULL_HANDLE);
    if (!m_scanSubgroup) m_params.kernel = 0;
    if (m_params.verify) prepareReadback(m_params.count());
  }

  void record(VkCommandBuffer cmd, const vk::FrameInfo& info) override {
    collectTimestamps(info.frameInFlight);
    const uint32_t q = info.frameInFlight * kTimestamps;  // this slot's first query

    const uint32_t n = m_params.count();
    const uint32_t width = m_params.gridWidth();
    const std::vector<ScanLevel> levels = scanLevels(n);
    const glm::vec4 params(m_time, info.frame.aspect(), 0.0f, 0.0f);
    auto push = [&](VkPipelineLayout layout, VkShaderStageFlags stage, const PassConstants& pc) {
      vkCmdPushConstants(cmd, layout, stage, 0, sizeof(pc), &pc);
    };

    // Last frame read these buffers in the vertex shader (and maybe copied them) and wrote them in compute.
    // GL needs nothing here (only incoherent writes before *reads* need its barrier).
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    // Queries must be reset before they're written again — in the command buffer, outside rendering.
    vkCmdResetQueryPool(cmd, m_queries, q, kTimestamps);
    // ALL_COMMANDS: the timestamp is written once everything before it has finished. GL_TIMESTAMP behaves so.
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, q + 0);

    vk::debug::Label label(cmd, "compaction (compute)");
    // Compute bind point: pipeline + set. The graphics bind point (below) needs its own vkCmdBindDescriptorSets.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1, &m_set, 0, nullptr);

    // --- flags ----------------------------------------------------------------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_flagsPipeline);
    push(m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, {{n, 0, 0, width}, params});
    vkCmdDispatch(cmd, groups(n), 1, 1);
    computeToCompute(cmd);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, q + 1);

    // --- scan: up the levels, then add each level's block offsets back down ----------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_params.kernel == 1 ? m_scanSubgroup : m_scanShared);
    for (size_t k = 0; k < levels.size(); ++k) {
      push(m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
           {{levels[k].count, levels[k].offset, sumsOffset(levels, k), width}, params});
      vkCmdDispatch(cmd, levels[k].blocks, 1, 1);
      computeToCompute(cmd);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_addPipeline);
    for (size_t k = levels.size() - 1; k-- > 0;) {
      push(m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
           {{levels[k].count, levels[k].offset, levels[k + 1].offset, width}, params});
      vkCmdDispatch(cmd, groups(levels[k].count), 1, 1);
      computeToCompute(cmd);
    }
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, q + 2);

    // --- scatter --------------------------------------------------------------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterPipeline);
    push(m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, {{n, 0, 0, width}, params});
    vkCmdDispatch(cmd, groups(n), 1, 1);
    // Next uses: storage reads in the vertex shader (GL: GL_SHADER_STORAGE_BARRIER_BIT), and copies for the check.
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, q + 3);
    m_written[info.frameInFlight] = true;

    if (m_params.verify) recordReadback(cmd, n);

    // --- draw -----------------------------------------------------------------------------------------------
    vk::beginRendering(cmd, info.extent, {.color = info.targetView, .clearColor = {0.02f, 0.02f, 0.03f, 1.0f}});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pointsPipeline);
    vk::setFlippedViewport(cmd, info.extent);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawLayout, 0, 1, &m_set, 0, nullptr);
    for (float mode : {0.0f, 1.0f}) {
      if (mode == 0.0f && !m_params.showRejected) continue;
      const glm::vec4 drawParams(m_time, info.frame.aspect(), mode, 0.0f);
      push(m_drawLayout, VK_SHADER_STAGE_VERTEX_BIT, {{n, 0, 0, width}, drawParams});
      vkCmdDraw(cmd, n, 1, 0, 0);  // mode 1: vertices past the (GPU-side) count clip themselves
    }
    vkCmdEndRendering(cmd);
  }

  void ui() override { paramsUi(m_params, m_timings, m_verifyResult, m_subgroupSize); }

 private:
  // This slot's fence has been waited on before record(), so its last timestamps are final: no WAIT flag, and
  // never a stall. timestampPeriod converts ticks to nanoseconds (1 on NVIDIA, 10 on AMD).
  void collectTimestamps(uint32_t frame) {
    if (!m_written[frame]) return;
    std::array<uint64_t, kTimestamps> t{};
    const VkResult result =
        vkGetQueryPoolResults(m_ctx->device, m_queries, frame * kTimestamps, kTimestamps, sizeof(t), t.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) return;
    const double ms = double(m_ctx->properties.limits.timestampPeriod) * 1e-6;
    m_timings = {double(t[1] - t[0]) * ms, double(t[2] - t[1]) * ms, double(t[3] - t[2]) * ms};
    m_timingLog.add(m_timings, m_autoVerify.frame);
  }

  // --- the CPU check: copy out in the frame, read after the GPU is done ---------------------------------------
  void prepareReadback(uint32_t n) {
    const VkDeviceSize size = (3ull * n + 1) * sizeof(uint32_t);
    if (m_readback.size() < size) {
      m_readback = vk::Buffer(*m_ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT,  // CPU *reads* it: cached is much faster
                              "15 readback");
    }
  }

  void recordReadback(VkCommandBuffer cmd, uint32_t n) {
    const VkDeviceSize words = n * sizeof(uint32_t);
    const VkBufferCopy flags{0, 0, words}, scanned{0, words, words}, compact{0, 2 * words, words + 4};
    vkCmdCopyBuffer(cmd, m_flags.handle(), m_readback.handle(), 1, &flags);
    vkCmdCopyBuffer(cmd, m_scan.handle(), m_readback.handle(), 1, &scanned);
    vkCmdCopyBuffer(cmd, m_compact.handle(), m_readback.handle(), 1, &compact);
    // Make the copies visible to the host once the fence signals (the HOST stage: the CPU reading the mapping).
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                  VK_ACCESS_2_HOST_READ_BIT);
    m_readbackCount = n;
    m_verifyKernel = m_params.kernel;
    m_readbackPending = true;
    m_params.verify = false;
  }

  void finishVerify() {
    VK_CHECK(vkDeviceWaitIdle(m_ctx->device));  // a full stall, like GL's glGetNamedBufferSubData — fine for a button
    const uint32_t n = m_readbackCount;
    const auto* words = static_cast<const uint32_t*>(m_readback.mapped());
    const std::vector<uint32_t> flags(words, words + n), scanned(words + n, words + 2 * n),
        compact(words + 2 * n, words + 3 * n + 1);
    m_verifyResult = verify(flags, scanned, compact, n);
    log::info("15_prefix_sum verify ({}): {}", kKernels[m_verifyKernel], m_verifyResult);
    m_readbackPending = false;
  }

  Params m_params;
  float m_time = 0.0f;
  uint32_t m_subgroupSize = 0;
  vk::Context* m_ctx = nullptr;
  vk::Buffer m_scan, m_flags, m_compact, m_readback;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  VkDescriptorSet m_set = VK_NULL_HANDLE;
  VkPipelineLayout m_computeLayout = VK_NULL_HANDLE, m_drawLayout = VK_NULL_HANDLE;
  VkPipeline m_flagsPipeline = VK_NULL_HANDLE, m_scanShared = VK_NULL_HANDLE, m_scanSubgroup = VK_NULL_HANDLE,
             m_addPipeline = VK_NULL_HANDLE, m_scatterPipeline = VK_NULL_HANDLE, m_pointsPipeline = VK_NULL_HANDLE;
  VkQueryPool m_queries = VK_NULL_HANDLE;
  std::array<bool, vk::kFramesInFlight> m_written{};
  Timings m_timings;
  bool m_readbackPending = false;
  uint32_t m_readbackCount = 0;
  int m_verifyKernel = 0;
  std::string m_verifyResult;
  AutoVerify m_autoVerify;
  TimingLog m_timingLog;
};

GLINT_REGISTER_VK("15_prefix_sum", PrefixSumVK);

}  // namespace glint::prefix_sum
