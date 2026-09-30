#version 460

// Same as triangle.gl.vert, except for how the matrix arrives.

layout(location = 0) in vec2 inPosition;
layout(location = 4) in vec3 inColor;

// VK: no loose uniforms. A push constant block is a few bytes written straight into the command buffer
// (vkCmdPushConstants) — the closest thing to GL's glProgramUniform* for small per-draw data.
layout(push_constant) uniform PushConstants {
  mat4 transform;
} pc;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inColor;
  gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
}
