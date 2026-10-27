// 16_noise — improved Perlin noise (Perlin 2002; GPU Gems 1 ch5, GPU Gems 2 ch26's inoise.fxh), fractal sums of it,
// and Worley cellular noise. Everything takes an integer *period* and wraps the lattice with it, so a texture
// that covers [0, period) tiles seamlessly. noise_cpu.h is the same code in C++ (the CPU check compares them).
#ifndef NOISE_GLSL
#define NOISE_GLSL

const int kPerm[256] = int[256](
#include "permutation.inc"
);

// The 12 cube-edge directions, padded to 16 (4 repeated) so a hash & 15 picks one — GG2 ch26's gradient table.
const vec3 kGrad[16] = vec3[16](vec3(1, 1, 0), vec3(-1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0),
                                vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
                                vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, 1, -1), vec3(0, -1, -1),
                                vec3(1, 1, 0), vec3(0, -1, 1), vec3(-1, 1, 0), vec3(0, -1, -1));

int perm(int i) { return kPerm[i & 255]; }
// % on a negative int is undefined in GLSL (Worley's neighbor cells reach -1): shift positive first.
int wrap(int i, int period) { return (i + 1024 * period) % period; }

// The "improved" fade: 6t^5 - 15t^4 + 10t^3 has zero 1st *and* 2nd derivative at 0 and 1, so the noise's
// derivative (normals, bump maps) has no creases at cell borders. Perlin's 1985 curve was 3t^2 - 2t^3.
vec3 fade(vec3 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// Gradient at lattice corner c (already wrapped), dotted with the offset from that corner.
float corner(ivec3 c, vec3 offset) { return dot(kGrad[perm(perm(perm(c.x) + c.y) + c.z) & 15], offset); }

// Improved Perlin noise, roughly [-1, 1], periodic with `period` lattice cells on every axis.
float inoise(vec3 p, int period) {
  ivec3 i0 = ivec3(floor(p));
  vec3 f = p - floor(p);
  vec3 u = fade(f);
  ivec3 a = ivec3(wrap(i0.x, period), wrap(i0.y, period), wrap(i0.z, period));
  ivec3 b = ivec3(wrap(i0.x + 1, period), wrap(i0.y + 1, period), wrap(i0.z + 1, period));
  return mix(mix(mix(corner(ivec3(a.x, a.y, a.z), f), corner(ivec3(b.x, a.y, a.z), f - vec3(1, 0, 0)), u.x),
                 mix(corner(ivec3(a.x, b.y, a.z), f - vec3(0, 1, 0)), corner(ivec3(b.x, b.y, a.z), f - vec3(1, 1, 0)),
                     u.x),
                 u.y),
             mix(mix(corner(ivec3(a.x, a.y, b.z), f - vec3(0, 0, 1)), corner(ivec3(b.x, a.y, b.z), f - vec3(1, 0, 1)),
                     u.x),
                 mix(corner(ivec3(a.x, b.y, b.z), f - vec3(0, 1, 1)), corner(ivec3(b.x, b.y, b.z), f - vec3(1, 1, 1)),
                     u.x),
                 u.y),
             u.z);
}

// Fractal sums, as in inoise.fxh (lacunarity 2: each octave doubles the frequency, so the period doubles too and
// every octave still tiles).
float fbm(vec3 p, int period, int octaves, float gain) {
  float sum = 0.0, amp = 0.5;
  for (int o = 0; o < octaves; ++o) {
    sum += inoise(p, period) * amp;
    p *= 2.0;
    period *= 2;
    amp *= gain;
  }
  return sum;
}

float turbulence(vec3 p, int period, int octaves, float gain) {
  float sum = 0.0, amp = 1.0;
  for (int o = 0; o < octaves; ++o) {
    sum += abs(inoise(p, period)) * amp;
    p *= 2.0;
    period *= 2;
    amp *= gain;
  }
  return sum;
}

// Ridged multifractal (Musgrave, "Texturing & Modeling" ch12): sharp crests where the noise crosses zero, each
// octave weighted by the previous one so detail gathers on the ridges.
float ridgedmf(vec3 p, int period, int octaves, float gain) {
  float sum = 0.0, amp = 0.5, prev = 1.0;
  for (int o = 0; o < octaves; ++o) {
    float n = 1.0 - abs(inoise(p, period));
    n *= n;
    sum += n * amp * prev;
    prev = n;
    p *= 2.0;
    period *= 2;
    amp *= gain;
  }
  return sum;
}

uint pcg(uint v) {
  uint state = v * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// Worley F1 (Worley 1996): distance to the nearest of one random feature point per cell, cells = period^3.
// The 27 neighbor cells cover every point that can be nearest.
float worley(vec3 p, int period, uint seed) {
  ivec3 cell = ivec3(floor(p));
  float best = 1e9;
  for (int z = -1; z <= 1; ++z) {
    for (int y = -1; y <= 1; ++y) {
      for (int x = -1; x <= 1; ++x) {
        ivec3 c = cell + ivec3(x, y, z);
        ivec3 w = ivec3(wrap(c.x, period), wrap(c.y, period), wrap(c.z, period));
        uint h = pcg(uint(w.x) + pcg(uint(w.y) + pcg(uint(w.z) + seed)));
        vec3 feature = vec3(c) + vec3(h & 1023u, (h >> 10) & 1023u, (h >> 20) & 1023u) / 1024.0;
        vec3 d = feature - p;
        best = min(best, dot(d, d));
      }
    }
  }
  return sqrt(best);
}

// The four channels of the 3D texture, each mapped to [0, 1] (so they display directly and fit any format).
vec4 noiseChannels(vec3 p, int period, int octaves, float gain, int worleyCells, uint seed) {
  float r = clamp(0.5 + 0.7 * fbm(p, period, octaves, gain), 0.0, 1.0);
  float g = clamp(turbulence(p, period, octaves, gain) * 0.6, 0.0, 1.0);
  float b = clamp(ridgedmf(p, period, octaves, gain), 0.0, 1.0);
  vec3 q = p * (float(worleyCells) / float(period));  // same texture extent, worleyCells cells across
  float a = clamp(1.0 - worley(q, worleyCells, seed) * 1.2, 0.0, 1.0);
  return vec4(r, g, b, a);
}

#endif
