#version 460
#include "glint.glsl"
#include "color.glsl"

// Mode 0: the four channels as 2D slices (2x2 grid), scrolling through z, each repeated `tiles` times to show
//         that the texture wraps without seams.
// Mode 1: one channel ray-marched as a volume in the cube [-1, 1]^3 (emission-absorption, one light tap).
// The 3D texture is sampled like any other: trilinear filtering, REPEAT on all three axes (incl. R/W).

layout(location = 0) in vec2 vNdc;

SAMPLER(0, 0) sampler3D uNoise;

PUSH_CONSTANTS Display {
  mat4 inverseViewProjection;
  vec4 eye;
  vec4 view;    // x = mode, y = channel, z = slice depth, w = aspect
  vec4 volume;  // x = density, y = coverage, z = tiles
} pc;

layout(location = 0) out vec4 outColor;

vec3 slices() {
  float aspect = pc.view.w;
  vec2 q = aspect > 1.0 ? vec2(vNdc.x * aspect, vNdc.y) : vec2(vNdc.x, vNdc.y / aspect);  // square, [-1,1]
  q /= 0.95;
  if (any(greaterThan(abs(q), vec2(1.0)))) return vec3(0.03);
  vec2 cell = q * 0.5 + 0.5;  // [0,1] over the grid
  ivec2 quadrant = ivec2(cell * 2.0);
  vec2 uv = fract(cell * 2.0);
  if (any(lessThan(uv, vec2(0.006))) || any(greaterThan(uv, vec2(0.994)))) return vec3(0.03);  // grid lines
  int channel = quadrant.y == 1 ? quadrant.x : 2 + quadrant.x;  // top row R G, bottom row B A
  float v = texture(uNoise, vec3(uv * pc.volume.z, pc.view.z))[channel];
  return vec3(v);
}

vec3 volumeMarch() {
  // Camera ray through this pixel: unproject the near and far plane points. Near is z = -1 in GL's NDC but z = 0
  // in VK's (ClipDepth::ZeroToOne) — the one per-API line in this shader.
#ifdef VULKAN
  const float nearZ = 0.0;
#else
  const float nearZ = -1.0;
#endif
  vec4 a = pc.inverseViewProjection * vec4(vNdc, nearZ, 1.0);
  vec4 b = pc.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
  vec3 ro = pc.eye.xyz;
  vec3 rd = normalize(b.xyz / b.w - a.xyz / a.w);

  // Ray vs the cube [-1, 1]^3 (slab method).
  vec3 t0 = (-1.0 - ro) / rd, t1 = (1.0 - ro) / rd;
  float tNear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  float tFar = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  vec3 background = mix(vec3(0.02, 0.025, 0.04), vec3(0.08, 0.1, 0.16), vNdc.y * 0.5 + 0.5);
  if (tFar <= max(tNear, 0.0)) return background;

  int channel = int(pc.view.y);
  const int kSteps = 128;
  float dt = (tFar - max(tNear, 0.0)) / float(kSteps);
  vec3 lightDir = normalize(vec3(0.6, 1.0, 0.3));
  float transmittance = 1.0;
  vec3 color = vec3(0.0);
  for (int i = 0; i < kSteps && transmittance > 0.01; ++i) {
    vec3 p = ro + rd * (max(tNear, 0.0) + (float(i) + 0.5) * dt);
    float d = max(texture(uNoise, p * 0.5 + 0.5)[channel] - pc.volume.y, 0.0) * pc.volume.x;
    if (d <= 0.0) continue;
    // One tap toward the light: less density there = more light reaches this point.
    float toward = max(texture(uNoise, (p + lightDir * 0.15) * 0.5 + 0.5)[channel] - pc.volume.y, 0.0);
    vec3 lit = mix(vec3(0.12, 0.15, 0.25), vec3(1.0, 0.92, 0.8), exp(-toward * pc.volume.x * 0.6));
    float absorbed = 1.0 - exp(-d * dt);
    color += transmittance * absorbed * lit;
    transmittance *= 1.0 - absorbed;
  }
  return color + transmittance * background;
}

void main() {
  // Slices show the stored values as they are (data, not light). The volume is lit, so it's linear light:
  // encode it for the UNORM swapchain.
  outColor = vec4(pc.view.x < 0.5 ? slices() : linearToSrgb(volumeMarch()), 1.0);
}
