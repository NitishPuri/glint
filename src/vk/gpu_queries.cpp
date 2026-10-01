#include "vk/gpu_queries.h"

#include <vector>

#include "core/log.h"
#include "vk/debug.h"

namespace glint::vk {

namespace {
// In bit order — which is also the order the results come back in.
constexpr VkQueryPipelineStatisticFlags kStatistics =
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
}  // namespace

GpuQueries::GpuQueries(const Context& ctx) : m_ctx(ctx) {
  // Timestamps tick at timestampPeriod nanoseconds; only timestampValidBits of each value are meaningful.
  m_nsPerTick = double(ctx.properties.limits.timestampPeriod);
  uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &familyCount, families.data());
  const uint32_t validBits = families[ctx.queueFamily].timestampValidBits;
  m_validMask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1;
  log::info("GPU timing: timestampPeriod {} ns, {} valid bits, pipeline statistics {}", m_nsPerTick, validBits,
            ctx.pipelineStatistics ? "on" : "unsupported");

  VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  info.queryCount = 2 * kFramesInFlight;
  VK_CHECK(vkCreateQueryPool(ctx.device, &info, nullptr, &m_timestamps));
  debug::setName(ctx.device, VK_OBJECT_TYPE_QUERY_POOL, m_timestamps, "timestamps");
  if (ctx.pipelineStatistics) {
    info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    info.queryCount = kFramesInFlight;
    info.pipelineStatistics = kStatistics;
    VK_CHECK(vkCreateQueryPool(ctx.device, &info, nullptr, &m_statistics));
    debug::setName(ctx.device, VK_OBJECT_TYPE_QUERY_POOL, m_statistics, "pipeline statistics");
  }
}

GpuQueries::~GpuQueries() {
  vkDestroyQueryPool(m_ctx.device, m_timestamps, nullptr);
  vkDestroyQueryPool(m_ctx.device, m_statistics, nullptr);
}

void GpuQueries::collect(uint32_t frame) {
  if (!m_written[frame]) return;  // nothing recorded into this slot yet (first frames)
  // The slot's fence has signalled, so the results are final: no WAIT flag needed, and no stall.
  uint64_t ticks[2] = {};
  if (vkGetQueryPoolResults(m_ctx.device, m_timestamps, 2 * frame, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                            VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
    return;
  }
  m_stats.valid = true;
  m_stats.gpuMs = double((ticks[1] - ticks[0]) & m_validMask) * m_nsPerTick * 1e-6;
  if (m_statistics) {
    uint64_t values[4] = {};
    if (vkGetQueryPoolResults(m_ctx.device, m_statistics, frame, 1, sizeof(values), values, sizeof(values),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
      m_stats.hasPipelineStatistics = true;
      m_stats.vertices = values[0];
      m_stats.primitives = values[1];
      m_stats.vertexInvocations = values[2];
      m_stats.fragmentInvocations = values[3];
    }
  }
}

void GpuQueries::begin(VkCommandBuffer cmd, uint32_t frame) {
  // Queries must be reset before reuse; doing it in the same command buffer keeps it in GPU order.
  vkCmdResetQueryPool(cmd, m_timestamps, 2 * frame, 2);
  if (m_statistics) vkCmdResetQueryPool(cmd, m_statistics, frame, 1);
  // ALL_COMMANDS: written once everything before it has finished (sync2: there is no TOP_OF_PIPE-only timing).
  vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_timestamps, 2 * frame);
  // Begun outside rendering, so it may span all of the technique's passes.
  if (m_statistics) vkCmdBeginQuery(cmd, m_statistics, frame, 0);
}

void GpuQueries::end(VkCommandBuffer cmd, uint32_t frame) {
  if (m_statistics) vkCmdEndQuery(cmd, m_statistics, frame);
  vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_timestamps, 2 * frame + 1);
  m_written[frame] = true;
}

}  // namespace glint::vk
