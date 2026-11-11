#version 460
#include "glint.glsl"
#include "grass.glsl"

// The ground: one quad from VERTEX_ID (4-vertex triangle strip, no vertex buffer), at y = 0.

layout(location = 0) out vec3 vWorld;

void main() {
  vec2 corner = vec2(VERTEX_ID & 1, VERTEX_ID >> 1) * 2.0 - 1.0;  // (-1,-1) (1,-1) (-1,1) (1,1)
  vWorld = vec3(corner.x, 0.0, corner.y) * pc.field.x;
  gl_Position = pc.viewProjection * vec4(vWorld, 1.0);
}
