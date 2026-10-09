#version 460
#include "glint.glsl"
#include "color.glsl"

// Final pass: HDR scene + bloom -> exposure -> tone map -> sRGB encode -> color grading LUT.
// Bloom is added in linear HDR, *before* tone mapping: it is light (as if scattered in the lens/eye), so it
// must be exposed and compressed together with the scene.

layout(location = 0) in vec2 vUv;

SAMPLER(0, 0) sampler2D uHdr;
SAMPLER(0, 1) sampler2D uBloom;  // bloom level 0 (half resolution; bilinear upscale is invisible on a glow)
SAMPLER(0, 2) sampler2D uLut;    // (N*N) x N strip, RGBA8 UNORM

PUSH_CONSTANTS Composite {
  vec4 tonemap;  // x = exposure scale, y = operator, z = 1: grading LUT
  vec4 bloom;    // x = bloom scale, y = view (0 final, 1 bloom only, 2 no bloom)
} pc;

layout(location = 0) out vec4 outColor;

// GG2 ch24's 3D lookup with a 2D strip: bilinear inside a 32x32 (r, g) slice, a manual lerp between the two
// slices around b. Half-texel offsets put 0 and 1 at texel *centers*, so the table's end entries are exact.
vec3 applyLut(vec3 c) {
  const float n = 32.0;
  c = clamp(c, 0.0, 1.0);
  float slice = c.b * (n - 1.0);
  float s0 = floor(slice), s1 = min(s0 + 1.0, n - 1.0);
  vec2 uv = vec2((c.r * (n - 1.0) + 0.5) / (n * n), (c.g * (n - 1.0) + 0.5) / n);
  vec3 a = texture(uLut, uv + vec2(s0 / n, 0.0)).rgb;
  vec3 b = texture(uLut, uv + vec2(s1 / n, 0.0)).rgb;
  return mix(a, b, slice - s0);
}

void main() {
  vec3 hdr = texture(uHdr, vUv).rgb;
  vec3 bloom = texture(uBloom, vUv).rgb * pc.bloom.x;
  vec3 color = pc.bloom.y < 0.5 ? hdr + bloom : (pc.bloom.y < 1.5 ? bloom : hdr);
  vec3 display = linearToSrgb(tonemap(color * pc.tonemap.x, pc.tonemap.y));
  if (pc.tonemap.z > 0.5) display = applyLut(display);
  outColor = vec4(display, 1.0);
}
