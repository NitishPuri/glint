#version 460

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vTangent;

layout(std140, set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;
  vec4 toLight;
  vec4 cameraPosition;
  vec4 settings;  // x ambient, y normal maps on
} frame;

// The same push-constant block the vertex stage declares (both stages share one range).
layout(push_constant) uniform Draw {
  mat4 model;
  vec4 baseColorFactor;
  vec4 material;  // x alpha cutoff (0 = opaque), y has normal map
} draw;

// Set 1 = per material: one descriptor set per material, written once at load; switching materials is
// one vkCmdBindDescriptorSets. GL: rebinding texture units 0 and 1.
layout(set = 1, binding = 0) uniform sampler2D uBaseColor;
layout(set = 1, binding = 1) uniform sampler2D uNormalMap;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 base = texture(uBaseColor, vUv) * draw.baseColorFactor;
  if (draw.material.x > 0.0 && base.a < draw.material.x) discard;  // glTF alphaMode MASK

  vec3 n = normalize(vNormal);
  if (frame.settings.y > 0.5 && draw.material.y > 0.5) {
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
