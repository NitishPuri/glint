#version 460
#include "glint.glsl"

// Tone-mapping pass: HDR (linear, unbounded) -> display (sRGB-encoded, [0,1]).
//   1. exposure: scale by 2^EV (a camera's exposure setting)
//   2. tone map: compress [0, inf) into [0, 1) without hard clipping
//   3. encode: linear -> sRGB, because the display interprets the bytes it gets as sRGB

layout(location = 0) in vec2 vUv;

SAMPLER(0, 0) sampler2D uHdr;

PUSH_CONSTANTS Tonemap {
  vec4 params;  // x = exposure scale, y = operator (0 clamp, 1 Reinhard, 2 ACES), z = 1: encode to sRGB
} pc;

layout(location = 0) out vec4 outColor;

vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Krzysztof Narkowicz's fit of the ACES filmic curve.
vec3 acesFitted(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
  vec2 uv = vUv;
#ifdef VULKAN
  uv.y = 1.0 - uv.y;  // VK render targets store the top row first (see 07); GL's start at the bottom
#endif
  vec3 hdr = texture(uHdr, uv).rgb * pc.params.x;
  vec3 mapped;
  if (pc.params.y < 0.5) {
    mapped = clamp(hdr, 0.0, 1.0);  // everything above 1 clips to white
  } else if (pc.params.y < 1.5) {
    mapped = hdr / (1.0 + hdr);  // Reinhard: never reaches 1, desaturates highlights gently
  } else {
    mapped = acesFitted(hdr);  // filmic: toe + shoulder, more contrast
  }
  // The swapchain / default framebuffer is UNORM in Glint (so GL and VK match), so encode here. With an
  // *_SRGB swapchain (VK) or GL_FRAMEBUFFER_SRGB on an sRGB-capable default framebuffer (GL), the hardware
  // would do this on write instead.
  outColor = vec4(pc.params.z > 0.5 ? linearToSrgb(mapped) : mapped, 1.0);
}
