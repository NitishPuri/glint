#version 460 core

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vTangent;

layout(std140, binding = 0) uniform Frame {
  mat4 viewProjection;
  vec4 toLight;
  vec4 cameraPosition;
  vec4 settings;  // x ambient, y normal maps on
} frame;

layout(location = 1) uniform vec4 uBaseColorFactor;
layout(location = 2) uniform vec4 uMaterial;  // x alpha cutoff (0 = opaque), y has normal map

// Per material: texture units 0 and 1, rebound by the app whenever the material changes.
layout(binding = 0) uniform sampler2D uBaseColor;
layout(binding = 1) uniform sampler2D uNormalMap;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 base = texture(uBaseColor, vUv) * uBaseColorFactor;
  if (uMaterial.x > 0.0 && base.a < uMaterial.x) discard;  // glTF alphaMode MASK

  vec3 n = normalize(vNormal);
  if (frame.settings.y > 0.5 && uMaterial.y > 0.5) {
    vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
    vec3 b = cross(n, t) * vTangent.w;
    vec3 tangentNormal = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;  // glTF normal maps: +Y up, no v flip
    n = normalize(mat3(t, b, n) * tangentNormal);
  }
  if (!gl_FrontFacing) n = -n;  // double-sided materials: light the back side too

  vec3 l = frame.toLight.xyz;
  vec3 v = normalize(frame.cameraPosition.xyz - vWorldPosition);
  vec3 h = normalize(l + v);
  float diffuse = max(dot(n, l), 0.0);
  float specular = pow(max(dot(n, h), 0.0), 32.0) * 0.2;
  outColor = vec4(base.rgb * (frame.settings.x + diffuse) + vec3(specular), 1.0);
}
