#include "gl/gpu_queries.h"

#include <cstddef>

namespace glint::gl {

namespace {
// Query targets, indexed like GpuQueries::Query.
constexpr GLenum kTargets[] = {GL_TIME_ELAPSED, GL_VERTICES_SUBMITTED, GL_PRIMITIVES_SUBMITTED,
                               GL_VERTEX_SHADER_INVOCATIONS, GL_FRAGMENT_SHADER_INVOCATIONS};
}  // namespace

GpuQueries::GpuQueries() {
  // Not glCreateQueries (DSA) here: Mesa rejects the pipeline-statistics targets there with GL_INVALID_ENUM
  // (the DSA target list predates ARB_pipeline_statistics_query). glGenQueries only reserves names; the
  // first glBeginQuery fixes each query's target.
  for (Set& set : m_sets) glGenQueries(kCount, set.ids.data());
}

GpuQueries::~GpuQueries() {
  for (Set& set : m_sets) glDeleteQueries(kCount, set.ids.data());
}

void GpuQueries::begin() {
  Set& set = m_sets[size_t(m_current)];
  if (set.pending) collect(set);  // this set was used kRing frames ago: read it before reusing it
  for (int q = 0; q < kCount; ++q) glBeginQuery(kTargets[q], set.ids[size_t(q)]);
}

void GpuQueries::end() {
  Set& set = m_sets[size_t(m_current)];
  for (int q = 0; q < kCount; ++q) glEndQuery(kTargets[q]);
  set.pending = true;
  m_current = (m_current + 1) % kRing;
}

void GpuQueries::collect(Set& set) {
  set.pending = false;
  GLint available = GL_FALSE;
  glGetQueryObjectiv(set.ids[kTime], GL_QUERY_RESULT_AVAILABLE, &available);
  if (!available) return;  // still not done after kRing frames: drop it rather than stall

  GLuint64 values[kCount] = {};
  for (int q = 0; q < kCount; ++q) glGetQueryObjectui64v(set.ids[size_t(q)], GL_QUERY_RESULT, &values[q]);
  m_stats.valid = true;
  m_stats.gpuMs = double(values[kTime]) * 1e-6;  // GL_TIME_ELAPSED is in nanoseconds
  m_stats.hasPipelineStatistics = true;
  m_stats.vertices = values[kVertices];
  m_stats.primitives = values[kPrimitives];
  m_stats.vertexInvocations = values[kVertexInvocations];
  m_stats.fragmentInvocations = values[kFragmentInvocations];
}

}  // namespace glint::gl
