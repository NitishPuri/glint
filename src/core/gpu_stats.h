#pragma once

// What each backend measures around a technique's GPU work, in API-neutral form for the stats panel and the
// run log. Results arrive a frame or two late: queries are read back only once the GPU has finished them,
// without stalling (GL: GL_QUERY_RESULT_AVAILABLE; VK: after that frame slot's fence).

#include <cstdint>

namespace glint {

struct GpuStats {
  bool valid = false;  // false until the first results come back
  double gpuMs = 0.0;  // GPU time of the technique's commands (not ImGui)

  // Pipeline statistics: counted by the GPU itself, so they also cover passes the app never "sees" as draws.
  bool hasPipelineStatistics = false;
  uint64_t vertices = 0;             // vertices fetched by input assembly
  uint64_t primitives = 0;           // triangles assembled
  uint64_t vertexInvocations = 0;    // vertex shader runs (< vertices when the post-transform cache hits)
  uint64_t fragmentInvocations = 0;  // fragment shader runs (overdraw shows up here)
};

}  // namespace glint
