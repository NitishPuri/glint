#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(std140, binding = 0) uniform Ubo {
  mat4 projection;
  mat4 modelView;
  vec4 lightPosition;  // view space
  vec4 color;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec3 vViewVec;
layout(location = 4) out vec3 vLightVec;

void main() {
  vec4 position = ubo.modelView * vec4(inPosition, 1.0);
  gl_Position = ubo.projection * position;
  vNormal = mat3(ubo.modelView) * inNormal;
  vColor = ubo.color.rgb;
  vUv = inUv;
  vLightVec = ubo.lightPosition.xyz - position.xyz;
  vViewVec = -position.xyz;
}
