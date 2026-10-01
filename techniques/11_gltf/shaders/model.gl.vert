#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

// Per frame: binding point 0.
layout(std140, binding = 0) uniform Frame {
  mat4 viewProjection;
  vec4 toLight;
  vec4 cameraPosition;
  vec4 settings;
} frame;

// Per draw: plain uniforms, set with glProgramUniform* before each draw. (VK: one push-constant block.)
layout(location = 0) uniform mat4 uModel;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vTangent;

void main() {
  vec4 world = uModel * vec4(inPosition, 1.0);
  gl_Position = frame.viewProjection * world;
  // Node matrices may scale non-uniformly: normals need the inverse transpose.
  mat3 normalMatrix = transpose(inverse(mat3(uModel)));
  vWorldPosition = world.xyz;
  vNormal = normalMatrix * inNormal;
  vTangent = vec4(mat3(uModel) * inTangent.xyz, inTangent.w);
  vUv = inUv;
}
