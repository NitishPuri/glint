#version 460

layout(location = 0) in vec3 inPosition;

// GL: `layout(location = 0) uniform mat4 uMvp` set with glProgramUniformMatrix4fv.
// VK: a uniform buffer, found through descriptor set 0, binding 0 (vkCmdBindDescriptorSets).
layout(set = 0, binding = 0) uniform Ubo {
  mat4 mvp;
} ubo;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inPosition * 0.5 + 0.5;
  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
}
