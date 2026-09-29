#version 460 core

layout(location = 0) in vec3 inPosition;

layout(location = 0) uniform mat4 uMvp;  // VK: in a uniform buffer, reached through a descriptor set

layout(location = 0) out vec3 vColor;

void main() {
  // Color from the model-space position: each corner of the [-1,1] cube gets its own RGB corner.
  vColor = inPosition * 0.5 + 0.5;
  gl_Position = uMvp * vec4(inPosition, 1.0);
}
