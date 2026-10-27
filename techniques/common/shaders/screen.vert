#version 460
#include "glint.glsl"

// Full-screen triangle that passes NDC on (not a texture coordinate), for passes that compute their own
// coordinates from it (16 slices + camera rays, 17 display). With the flipped VK viewport, NDC +y is up in both
// APIs, so no #ifdef. (common/shaders/fullscreen.vert instead gives a uv for sampling *render targets*.)

layout(location = 0) out vec2 vNdc;

void main() {
  vec2 p = vec2((VERTEX_ID << 1) & 2, VERTEX_ID & 2) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
