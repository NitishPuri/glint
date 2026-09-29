#version 460 core

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vPositionWorld;
layout(location = 2) in vec3 vLightDirTangent;
layout(location = 3) in vec3 vEyeDirTangent;

layout(std140, binding = 0) uniform Shading {
  mat4 mvp;
  mat4 view;
  mat4 model;
  vec4 lightPosition;
  vec4 lightColorPower;
  vec4 material;
} u;

// Three textures on units 0-2. VK: three combined-image-sampler bindings in one descriptor set.
layout(binding = 0) uniform sampler2D uDiffuse;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uSpecular;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 lightColor = u.lightColorPower.rgb;
  float lightPower = u.lightColorPower.a;

  vec3 diffuseColor = texture(uDiffuse, vUv).rgb;
  vec3 ambientColor = u.material.x * diffuseColor;
  vec3 specularColor = texture(uSpecular, vUv).rgb * u.material.y;

  // The normal map stores tangent-space normals as colors: [0,1] -> [-1,1]. (Like opengl-tutorial, it is
  // read with v flipped: that's how this particular map was authored.)
  vec3 n = u.material.w > 0.5 ? normalize(texture(uNormal, vec2(vUv.x, -vUv.y)).rgb * 2.0 - 1.0)
                              : vec3(0.0, 0.0, 1.0);  // tangent space: the unperturbed normal is +Z

  float distance = length(u.lightPosition.xyz - vPositionWorld);
  vec3 l = normalize(vLightDirTangent);
  float cosTheta = clamp(dot(n, l), 0.0, 1.0);
  vec3 e = normalize(vEyeDirTangent);
  vec3 r = reflect(-l, n);
  float cosAlpha = clamp(dot(e, r), 0.0, 1.0);

  vec3 color = ambientColor
             + diffuseColor * lightColor * lightPower * cosTheta / (distance * distance)
             + specularColor * lightColor * lightPower * pow(cosAlpha, 5.0) / (distance * distance);
  outColor = vec4(color, 1.0);
}
