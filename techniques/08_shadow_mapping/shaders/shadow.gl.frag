#version 460 core

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormalCamera;
layout(location = 2) in vec3 vEyeDirCamera;
layout(location = 3) in vec3 vLightDirCamera;
layout(location = 4) in vec4 vShadowCoord;

layout(std140, binding = 0) uniform Shadow {
  mat4 mvp;
  mat4 view;
  mat4 model;
  mat4 shadowMatrix;
  vec4 lightDirection;
  vec4 lightColorPower;
  vec4 material;  // x ambient, y specular, z bias, w PCF
} u;

layout(binding = 0) uniform sampler2D uDiffuse;
// A *shadow* sampler: texture(uShadowMap, vec3(uv, ref)) returns the fraction of texels with ref <= stored
// depth (with linear filtering the hardware compares 4 texels and blends: free 2x2 PCF).
// This only works if the sampler has compare mode on — GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE,
// the thing Glint_gl forgot (undefined behaviour). VK: VkSamplerCreateInfo::compareEnable.
layout(binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) out vec4 outColor;

const vec2 kPoisson[4] = vec2[](vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
                                vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760));

void main() {
  vec3 lightColor = u.lightColorPower.rgb;
  float lightPower = u.lightColorPower.a;
  vec3 diffuseColor = texture(uDiffuse, vUv).rgb;
  vec3 ambientColor = u.material.x * diffuseColor;
  vec3 specularColor = vec3(u.material.y);

  vec3 n = normalize(vNormalCamera);
  vec3 l = normalize(vLightDirCamera);
  float cosTheta = clamp(dot(n, l), 0.0, 1.0);
  vec3 e = normalize(vEyeDirCamera);
  vec3 r = reflect(-l, n);
  float cosAlpha = clamp(dot(e, r), 0.0, 1.0);

  // Perspective divide (a no-op for the orthographic light; needed for the spotlight — the original
  // divided only z). shadowMatrix already mapped x, y, z to [0,1].
  vec3 coord = vShadowCoord.xyz / vShadowCoord.w;
  float ref = coord.z - u.material.z;
  float visibility = 1.0;
  if (u.material.w > 0.5) {
    // 4 taps around the point, each already 2x2-filtered: fully shadowed = 1 - 4 * 0.2 = 0.2.
    for (int i = 0; i < 4; ++i) {
      visibility -= 0.2 * (1.0 - texture(uShadowMap, vec3(coord.xy + kPoisson[i] / 700.0, ref)));
    }
  } else {
    visibility = 0.2 + 0.8 * texture(uShadowMap, vec3(coord.xy, ref));
  }

  vec3 color = ambientColor
             + visibility * diffuseColor * lightColor * lightPower * cosTheta
             + visibility * specularColor * lightColor * lightPower * pow(cosAlpha, 5.0);
  outColor = vec4(color, 1.0);
}
