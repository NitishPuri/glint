#pragma once

// GPU timing + pipeline statistics around a technique's commands, with VK query pools.
//   timestamps:  vkCmdWriteTimestamp2 before and after; (t1 - t0) * timestampPeriod = nanoseconds
//   statistics:  a VK_QUERY_TYPE_PIPELINE_STATISTICS query around the same commands (optional feature)
// One slice of each pool per frame in flight. A slice is reset in the command buffer that writes it, and read
// back after that frame slot's fence has signalled (collect()), so reading never waits for the GPU.
// GL equivalent: gl/gpu_queries.h (GL_TIME_ELAPSED + GL_*_SUBMITTED/INVOCATIONS queries).

#include <array>

#include "core/gpu_stats.h"
#include "vk/context.h"
#include "vk/frame.h"

namespace glint::vk {

class GpuQueries {
 public:
  explicit GpuQueries(const Context& ctx);
  ~GpuQueries();
  GpuQueries(const GpuQueries&) = delete;
  GpuQueries& operator=(const GpuQueries&) = delete;

  // After waiting for frame slot `frame`'s fence: read what that slot measured last time.
  void collect(uint32_t frame);
  // Around the technique's record(); outside any vkCmdBeginRendering.
  void begin(VkCommandBuffer cmd, uint32_t frame);
  void end(VkCommandBuffer cmd, uint32_t frame);

  const GpuStats& stats() const { return m_stats; }

 private:
  const Context& m_ctx;
  VkQueryPool m_timestamps = VK_NULL_HANDLE;  // 2 per frame slot
  VkQueryPool m_statistics = VK_NULL_HANDLE;  // 1 per frame slot (if supported)
  double m_nsPerTick = 1.0;
  uint64_t m_validMask = ~0ull;  // timestampValidBits: the bits of a timestamp that are meaningful
  std::array<bool, kFramesInFlight> m_written{};
  GpuStats m_stats;
};

}  // namespace glint::vk
