#version 460
#include "glint.glsl"

// The particle buffer the compute shader just wrote, read as an ordinary *vertex buffer* (two vec4 attributes,
// stride 32). That's why the barrier between the passes names vertex *attribute* reads: GL
// GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT, VK VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT.

layout(location = 0) in vec4 inPosition;  // xyz, w = age
layout(location = 1) in vec4 inVelocity;  // xyz, w = lifetime

PUSH_CONSTANTS Draw {
  mat4 viewProjection;
  vec4 params;  // x = brightness
} draw;

layout(location = 0) out vec3 vColor;

void main() {
  float age = inPosition.w, life = inVelocity.w;
  if (age < 0.0) {  // not born: put it outside the clip volume (x > w) in both APIs, so it's dropped
    gl_Position = vec4(2.0, 0.0, 0.0, 1.0);
    gl_PointSize = 1.0;
    vColor = vec3(0.0);
    return;
  }
  gl_Position = draw.viewProjection * vec4(inPosition.xyz, 1.0);
  // Points must write gl_PointSize. VK: always (undefined otherwise), and sizes > 1 need the `largePoints`
  // feature. GL: only used with glEnable(GL_PROGRAM_POINT_SIZE), else glPointSize() applies.
  gl_PointSize = 1.0;

  // Color by speed (slow: deep orange, fast: blue-white), fade in over 0.2 s and out over the last second.
  float speed = length(inVelocity.xyz);
  vec3 color = mix(vec3(1.0, 0.25, 0.05), vec3(0.4, 0.7, 1.0), smoothstep(1.0, 6.0, speed));
  float fade = smoothstep(0.0, 0.2, age) * (1.0 - smoothstep(life - 1.0, life, age));
  vColor = color * fade * draw.params.x;
}
