#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 4) in vec3 inColor;

// GL: a plain uniform at an explicit location (GL 4.3+), set with glProgramUniformMatrix4fv.
// VK has no loose uniforms: the same matrix travels as a push constant there.
layout(location = 0) uniform mat4 uTransform;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inColor;
  gl_Position = uTransform * vec4(inPosition, 0.0, 1.0);
}
