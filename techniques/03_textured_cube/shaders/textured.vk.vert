#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;

layout(set = 0, binding = 0) uniform Ubo {
  mat4 mvp;
} ubo;

layout(location = 0) out vec2 vUv;

void main() {
  vUv = inUv;
  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
}
