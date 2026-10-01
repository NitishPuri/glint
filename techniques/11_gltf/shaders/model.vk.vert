#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

// Set 0 = per frame (one set per frame in flight, bound once per frame).
layout(std140, set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;
  vec4 toLight;
  vec4 cameraPosition;
  vec4 settings;
} frame;

// Per draw: push constants (vkCmdPushConstants before each draw). GL: three loose uniforms.
layout(push_constant) uniform Draw {
  mat4 model;
  vec4 baseColorFactor;
  vec4 material;
} draw;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vTangent;

void main() {
  vec4 world = draw.model * vec4(inPosition, 1.0);
  gl_Position = frame.viewProjection * world;
  mat3 normalMatrix = transpose(inverse(mat3(draw.model)));
  vWorldPosition = world.xyz;
  vNormal = normalMatrix * inNormal;
  vTangent = vec4(mat3(draw.model) * inTangent.xyz, inTangent.w);
  vUv = inUv;
}
