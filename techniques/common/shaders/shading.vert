#version 460
#include "glint.glsl"

// Standard shading, vertex stage. Lighting vectors are computed in camera space, where the eye is at 0.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

// Shared by GL and VK (see glint.glsl). The uniform block: GL binding point 0 / VK set 0, binding 0.
UBO(0, 0) Shading {
  mat4 mvp;
  mat4 view;
  mat4 model;
  vec4 lightPosition;
  vec4 lightColorPower;
  vec4 material;
} u;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vPositionWorld;
layout(location = 2) out vec3 vNormalCamera;
layout(location = 3) out vec3 vEyeDirCamera;
layout(location = 4) out vec3 vLightDirCamera;

void main() {
  gl_Position = u.mvp * vec4(inPosition, 1.0);
  vPositionWorld = (u.model * vec4(inPosition, 1.0)).xyz;

  vec3 positionCamera = (u.view * u.model * vec4(inPosition, 1.0)).xyz;
  vEyeDirCamera = -positionCamera;  // vertex -> eye
  vec3 lightCamera = (u.view * vec4(u.lightPosition.xyz, 1.0)).xyz;
  vLightDirCamera = lightCamera - positionCamera;  // vertex -> light

  // Fine as long as the model matrix has no non-uniform scale (else use the inverse transpose).
  vNormalCamera = (u.view * u.model * vec4(inNormal, 0.0)).xyz;
  vUv = inUv;
}
