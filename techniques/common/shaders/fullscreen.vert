#version 460
#include "glint.glsl"

// Full-screen triangle from the vertex index (07's trick, now shared: VERTEX_ID is gl_VertexID in GL and
// gl_VertexIndex in VK). vUv addresses a render target that was drawn the same way, in either API.

layout(location = 0) out vec2 vUv;

void main() {
  vec2 p = vec2((VERTEX_ID << 1) & 2, VERTEX_ID & 2);
  vUv = p;
#ifdef VULKAN
  // VK render targets store the top row first (see 07); GL's start at the bottom. With the flipped viewport,
  // screen bottom (p.y = 0) must read the image's *last* row, so flip v once here for every post pass.
  vUv.y = 1.0 - vUv.y;
#endif
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
