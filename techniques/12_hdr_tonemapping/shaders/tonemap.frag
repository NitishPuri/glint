#version 460
#include "glint.glsl"
#include "color.glsl"

// Tone-mapping pass: HDR (linear, unbounded) -> display (sRGB-encoded, [0,1]).
//   1. exposure: scale by 2^EV (a camera's exposure setting)
//   2. tone map: compress [0, inf) into [0, 1) without hard clipping (operators in common/shaders/color.glsl)
//   3. encode: linear -> sRGB, because the display interprets the bytes it gets as sRGB

layout(location = 0) in vec2 vUv;

SAMPLER(0, 0) sampler2D uHdr;

PUSH_CONSTANTS Tonemap {
  vec4 params;  // x = exposure scale, y = operator (0 clamp, 1 Reinhard, 2 ACES), z = 1: encode to sRGB
} pc;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 hdr = texture(uHdr, vUv).rgb * pc.params.x;  // vUv is already flipped for VK (common fullscreen.vert)
  vec3 mapped = tonemap(hdr, pc.params.y);
  // The swapchain / default framebuffer is UNORM in Glint (so GL and VK match), so encode here. With an
  // *_SRGB swapchain (VK) or GL_FRAMEBUFFER_SRGB on an sRGB-capable default framebuffer (GL), the hardware
  // would do this on write instead.
  outColor = vec4(pc.params.z > 0.5 ? linearToSrgb(mapped) : mapped, 1.0);
}
