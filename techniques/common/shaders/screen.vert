#version 460
#include "glint.glsl"

// Full-screen triangle that passes NDC on (not a texture coordinate): the display pass computes slice
// coordinates and camera rays from it. With the flipped VK viewport, NDC +y is up in both APIs, so no #ifdef.

layout(location = 0) out vec2 vNdc;

void main() {
  vec2 p = vec2((VERTEX_ID << 1) & 2, VERTEX_ID & 2) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
