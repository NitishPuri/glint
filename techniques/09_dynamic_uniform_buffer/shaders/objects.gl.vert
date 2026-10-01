#version 460 core

layout(location = 0) in vec3 inPosition;

// Binding point 0: per frame (one block). Binding point 1: per object — the app points it at a different
// 64-byte slice of one big buffer before each draw (glBindBufferRange).
layout(std140, binding = 0) uniform Camera {
  mat4 viewProjection;
} camera;
layout(std140, binding = 1) uniform Object {
  mat4 model;
} object;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inPosition * 0.5 + 0.5;
  gl_Position = camera.viewProjection * object.model * vec4(inPosition, 1.0);
}
