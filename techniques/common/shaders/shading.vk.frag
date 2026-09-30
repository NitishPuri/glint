#version 460

// Standard shading, fragment stage: ambient + diffuse (1/d^2 falloff) + specular (cos^5 of reflection angle).

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vPositionWorld;
layout(location = 2) in vec3 vNormalCamera;
layout(location = 3) in vec3 vEyeDirCamera;
layout(location = 4) in vec3 vLightDirCamera;

layout(std140, set = 0, binding = 0) uniform Shading {
  mat4 mvp;
  mat4 view;
  mat4 model;
  vec4 lightPosition;
  vec4 lightColorPower;
  vec4 material;
} u;

// GL: texture unit 0. VK: a combined image sampler at binding 1 of the same set.
layout(set = 0, binding = 1) uniform sampler2D uDiffuse;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 lightColor = u.lightColorPower.rgb;
  float lightPower = u.lightColorPower.a;

  vec3 diffuseColor = texture(uDiffuse, vUv).rgb;
  vec3 ambientColor = u.material.x * diffuseColor;
  vec3 specularColor = vec3(u.material.y);

  float distance = length(u.lightPosition.xyz - vPositionWorld);
  vec3 n = normalize(vNormalCamera);
  vec3 l = normalize(vLightDirCamera);
  float cosTheta = clamp(dot(n, l), 0.0, 1.0);
  vec3 e = normalize(vEyeDirCamera);
  vec3 r = reflect(-l, n);
  float cosAlpha = clamp(dot(e, r), 0.0, 1.0);

  vec3 color = ambientColor
             + diffuseColor * lightColor * lightPower * cosTheta / (distance * distance)
             + specularColor * lightColor * lightPower * pow(cosAlpha, 5.0) / (distance * distance);
  outColor = vec4(color, u.material.z);
}
