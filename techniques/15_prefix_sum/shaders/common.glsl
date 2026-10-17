// 15_prefix_sum — what every pass shares: the three storage buffers, the push constants, and the points.
#ifndef PREFIX_COMMON_GLSL
#define PREFIX_COMMON_GLSL

// The draw's vertex shader defines this as `readonly` before including: a *writable* storage buffer in a vertex
// shader counts as a vertex-stage store, which VK only allows with the vertexPipelineStoresAndAtomics feature.
#ifndef BUFFER_ACCESS
#define BUFFER_ACCESS
#endif

BUFFER_ACCESS SSBO(0, 0) Scan {
  uint data[];  // all scan levels back to back (params.h scanLevels)
};
BUFFER_ACCESS SSBO(0, 1) Flags {
  uint flags[];  // 0/1 per point, kept separately so the CPU check can recompute the scan
};
BUFFER_ACCESS SSBO(0, 2) Compact {
  uint compact[];  // [0] = selected count, [1 + k] = index of the k-th selected point
};

PUSH_CONSTANTS Pass {
  uvec4 counts;  // x = element count, y = offset, z = sums offset, w = grid width
  vec4 params;   // x = time, y = aspect, z = draw mode
} pc;

uint pcg(uint v) {
  uint state = v * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// Point i: cell (i % W, i / W) of a W x H grid over [-1, 1]^2, jittered inside its cell.
vec2 pointPosition(uint i, uint width, uint count) {
  uint height = count / width;
  uint h = pcg(i);
  vec2 jitter = vec2(h & 0xffffu, h >> 16) / 65536.0;
  vec2 cell = vec2(i % width, i / width);
  return (cell + jitter) / vec2(width, height) * 2.0 - 1.0;
}

// The selection: a rotating, breathing five-pointed star with a hole.
bool inside(vec2 p, float t) {
  float c = cos(0.4 * t), s = sin(0.4 * t);
  vec2 q = mat2(c, -s, s, c) * p;
  float r = length(q);
  float star = (0.55 + 0.22 * cos(5.0 * atan(q.y, q.x))) * (0.9 + 0.1 * sin(1.3 * t));
  return r < star && r > 0.18;
}

#endif
