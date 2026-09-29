#version 460 core

// Shadow pass: only positions, transformed into the light's clip space. No fragment shader at all —
// depth is written by the fixed-function depth test. (VK allows the same: a pipeline with no fragment stage.)

layout(location = 0) in vec3 inPosition;

layout(location = 0) uniform mat4 uLightMvp;

void main() {
  gl_Position = uLightMvp * vec4(inPosition, 1.0);
}
