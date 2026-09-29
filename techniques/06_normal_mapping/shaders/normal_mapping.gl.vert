#version 460 core

// Normal mapping, vertex stage: build the tangent frame (T, B, N) in camera space and move the light and
// eye directions into *tangent space*, where the normal map's normals live.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;  // xyz = tangent, w = handedness (bitangent = cross(N, T) * w)

layout(std140, binding = 0) uniform Shading {
  mat4 mvp;
  mat4 view;
  mat4 model;
  vec4 lightPosition;
  vec4 lightColorPower;
  vec4 material;  // x ambient, y specular, z alpha, w > 0.5: use the normal map
} u;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vPositionWorld;
layout(location = 2) out vec3 vLightDirTangent;
layout(location = 3) out vec3 vEyeDirTangent;

void main() {
  gl_Position = u.mvp * vec4(inPosition, 1.0);
  vPositionWorld = (u.model * vec4(inPosition, 1.0)).xyz;
  vUv = inUv;

  mat3 modelView = mat3(u.view * u.model);
  vec3 positionCamera = (u.view * u.model * vec4(inPosition, 1.0)).xyz;
  vec3 eyeDirCamera = -positionCamera;
  vec3 lightDirCamera = (u.view * vec4(u.lightPosition.xyz, 1.0)).xyz - positionCamera;

  vec3 n = modelView * inNormal;
  vec3 t = modelView * inTangent.xyz;
  vec3 b = cross(n, t) * inTangent.w;
  // Rows T, B, N: multiplying by this matrix projects a camera-space vector onto the tangent frame.
  mat3 toTangent = transpose(mat3(t, b, n));
  vLightDirTangent = toTangent * lightDirCamera;
  vEyeDirTangent = toTangent * eyeDirCamera;
}
