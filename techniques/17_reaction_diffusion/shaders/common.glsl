// What the seed pass and the CPU reference (rd_cpu.h) agree on: the hash and the initial state.
#ifndef RD_COMMON_GLSL
#define RD_COMMON_GLSL

uint pcg(uint v) {
  uint state = v * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// u = 1, v = 0 everywhere, except 12x12 squares in about one in six 32x32 cells: u = 0.5, v = 0.25 plus a
// little hashed noise (perfectly symmetric seeds grow perfectly symmetric patterns).
vec4 seedValue(ivec2 p) {
  ivec2 cell = p / 32;
  uint h = pcg(uint(cell.x) + pcg(uint(cell.y) + 17u));
  ivec2 local = p - cell * 32 - ivec2(10 + int(h % 8u), 10 + int((h >> 3) % 8u));
  if (h % 6u == 0u && local.x >= 0 && local.y >= 0 && local.x < 12 && local.y < 12) {
    float noise = float(pcg(uint(p.x) * 65537u + uint(p.y)) & 255u) / 255.0;
    return vec4(0.5, 0.25 + 0.05 * noise, 0.0, 1.0);
  }
  return vec4(1.0, 0.0, 0.0, 1.0);
}

#endif
