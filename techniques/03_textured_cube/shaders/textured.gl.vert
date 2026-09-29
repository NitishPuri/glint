#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;

layout(location = 0) uniform mat4 uMvp;

layout(location = 0) out vec2 vUv;

void main() {
  vUv = inUv;
  gl_Position = uMvp * vec4(inPosition, 1.0);
}
