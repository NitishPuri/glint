#version 460
#include "glint.glsl"
#include "color.glsl"

// Bloom downsample: source level (twice this target's size) -> this level. The 13-tap filter from Jimenez,
// "Next Generation Post Processing in Call of Duty: Advanced Warfare" (2014): five overlapping 4-tap boxes
// (one centered, four in the corners), weighted 0.5 / 4 x 0.125. Bilinear taps at texel corners make each
// tap a free 2x2 average, so 13 taps cover a 6x6 source footprint with no aliasing from skipped texels.
//
// First pass only (HDR -> level 0): the *prefilter*. Karis average (weight each box by 1 / (1 + luma)) stops a
// single very bright pixel from turning into a flickering blob, then a soft threshold keeps only the bright
// parts (GG1 ch21's "glow source").

layout(location = 0) in vec2 vUv;

// GL: a texture *view* of one level (glTextureView). VK: a VkImageView of one mip level. Either way,
// textureSize(lod 0) is that level's size, and nothing can sample the level being rendered.
SAMPLER(0, 0) sampler2D uSource;

PUSH_CONSTANTS Downsample {
  vec4 params;  // x = 1: first pass (prefilter), y = threshold, z = knee, w = 1: Karis average
} pc;

layout(location = 0) out vec4 outColor;

vec3 tap(vec2 offset) { return texture(uSource, vUv + offset).rgb; }

float karisWeight(vec3 box) { return 1.0 / (1.0 + luminance(box)); }

void main() {
  vec2 t = 1.0 / vec2(textureSize(uSource, 0));  // one *source* texel
  vec3 a = tap(t * vec2(-2, 2)), b = tap(t * vec2(0, 2)), c = tap(t * vec2(2, 2));
  vec3 d = tap(t * vec2(-2, 0)), e = tap(vec2(0)), f = tap(t * vec2(2, 0));
  vec3 g = tap(t * vec2(-2, -2)), h = tap(t * vec2(0, -2)), i = tap(t * vec2(2, -2));
  vec3 j = tap(t * vec2(-1, 1)), k = tap(t * vec2(1, 1));
  vec3 l = tap(t * vec2(-1, -1)), m = tap(t * vec2(1, -1));

  vec3 boxes[5] = vec3[5]((j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                          (d + e + g + h) * 0.25, (e + f + h + i) * 0.25);
  float weights[5] = float[5](0.5, 0.125, 0.125, 0.125, 0.125);

  bool first = pc.params.x > 0.5;
  vec3 color = vec3(0.0);
  float total = 0.0;
  for (int n = 0; n < 5; ++n) {
    float w = weights[n] * (first && pc.params.w > 0.5 ? karisWeight(boxes[n]) : 1.0);
    color += boxes[n] * w;
    total += w;
  }
  color /= total;

  if (first) {
    // Soft threshold: 0 below (threshold - knee), quadratic ramp across the knee, linear above.
    float threshold = pc.params.y, knee = pc.params.z;
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    color *= max(soft, brightness - threshold) / max(brightness, 1e-5);
  }
  outColor = vec4(max(color, 0.0), 1.0);
}
