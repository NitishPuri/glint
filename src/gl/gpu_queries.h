#pragma once

// GPU timing + pipeline statistics around a technique's render(), with GL query objects.
//   GL_TIME_ELAPSED:            GPU time between glBeginQuery and glEndQuery
//   GL_VERTICES_SUBMITTED ...:  pipeline statistics (ARB_pipeline_statistics_query, core in GL 4.6)
// Several query targets can be active at once (one query per target). The results are read a few frames
// later from a ring of query sets, only when GL_QUERY_RESULT_AVAILABLE says they're ready, so the CPU never
// waits for the GPU. VK equivalent: vk/gpu_queries.h (timestamps + a pipeline-statistics query pool).

#include <array>
#include <cstddef>

#include "core/gpu_stats.h"
#include "gl/gl.h"

namespace glint::gl {

class GpuQueries {
 public:
  GpuQueries();
  ~GpuQueries();
  GpuQueries(const GpuQueries&) = delete;
  GpuQueries& operator=(const GpuQueries&) = delete;

  void begin();  // before the technique's render()
  void end();    // after it

  // Latest results that have come back (a few frames old).
  const GpuStats& stats() const { return m_stats; }

 private:
  static constexpr int kRing = 4;  // frames a result may take to come back before its set is reused
  enum Query { kTime, kVertices, kPrimitives, kVertexInvocations, kFragmentInvocations, kCount };
  struct Set {
    std::array<GLuint, kCount> ids{};
    bool pending = false;
  };
  void collect(Set& set);

  std::array<Set, kRing> m_sets;
  int m_current = 0;
  GpuStats m_stats;
};

}  // namespace glint::gl
