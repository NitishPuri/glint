#version 460
#include "glint.glsl"
#define BUFFER_ACCESS readonly
#include "common.glsl"

// Mode 0: every point, dimmed. Mode 1: the compacted list — vertex k draws compact[1 + k], colored by k / count.
// The CPU doesn't know the count (it's on the GPU), so mode 1 is drawn with N vertices and the ones past the
// count are clipped. An *indirect* draw (18_gpu_culling) takes the count straight from the buffer instead.
// This vertex shader reads a storage buffer the compute passes wrote: GL GL_SHADER_STORAGE_BARRIER_BIT, VK
// VERTEX_SHADER + SHADER_STORAGE_READ (14 read its buffer as vertex *attributes* instead).

layout(location = 0) out vec3 vColor;

vec3 hue(float h) { return clamp(abs(fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0); }

void main() {
  uint k = uint(VERTEX_ID);
  uint index = k;
  vColor = vec3(0.12);
  gl_PointSize = 1.0;
  if (pc.params.z > 0.5) {
    uint count = compact[0];
    if (k >= count) {
      gl_Position = vec4(2.0, 0.0, 0.0, 1.0);  // outside the clip volume in both APIs
      return;
    }
    index = compact[1u + k];
    vColor = hue(0.8 * float(k) / float(count));
  }
  vec2 p = pointPosition(index, pc.counts.w, pc.counts.x) * 0.95;
  float aspect = pc.params.y;
  gl_Position = vec4(aspect > 1.0 ? vec2(p.x / aspect, p.y) : vec2(p.x, p.y * aspect), 0.0, 1.0);
}
