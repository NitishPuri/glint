#version 460

layout(location = 0) in vec3 inPosition;

// Binding 0: per frame. Binding 1: per object — a UNIFORM_BUFFER_DYNAMIC descriptor; each draw passes a
// different offset into one big buffer to vkCmdBindDescriptorSets. The shader can't tell the difference.
layout(std140, set = 0, binding = 0) uniform Camera {
  mat4 viewProjection;
} camera;
layout(std140, set = 0, binding = 1) uniform Object {
  mat4 model;
} object;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inPosition * 0.5 + 0.5;
  gl_Position = camera.viewProjection * object.model * vec4(inPosition, 1.0);
}
