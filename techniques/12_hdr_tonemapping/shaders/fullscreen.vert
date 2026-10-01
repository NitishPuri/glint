#version 460
#include "glint.glsl"

// Full-screen triangle from the vertex index (07's trick, now shared: VERTEX_ID is gl_VertexID in GL and
// gl_VertexIndex in VK).

layout(location = 0) out vec2 vUv;

void main() {
  vec2 p = vec2((VERTEX_ID << 1) & 2, VERTEX_ID & 2);
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
