#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(std140, binding = 0) uniform Shadow {
  mat4 mvp;
  mat4 view;
  mat4 model;
  mat4 shadowMatrix;
  vec4 lightDirection;
  vec4 lightColorPower;
  vec4 material;
} u;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormalCamera;
layout(location = 2) out vec3 vEyeDirCamera;
layout(location = 3) out vec3 vLightDirCamera;
layout(location = 4) out vec4 vShadowCoord;

void main() {
  vec4 world = u.model * vec4(inPosition, 1.0);
  gl_Position = u.mvp * vec4(inPosition, 1.0);
  vShadowCoord = u.shadowMatrix * world;

  vEyeDirCamera = -(u.view * world).xyz;
  vLightDirCamera = (u.view * vec4(u.lightDirection.xyz, 0.0)).xyz;  // w = 0: a direction, not a point
  vNormalCamera = (u.view * u.model * vec4(inNormal, 0.0)).xyz;
  vUv = inUv;
}
