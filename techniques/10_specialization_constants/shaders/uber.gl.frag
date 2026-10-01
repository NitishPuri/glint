#version 460 core

// GL variant of the uber shader: the app compiles this source three times, each time inserting
//   #define LIGHTING_MODEL <0|1|2>
//   #define TOON_DESATURATION <float>
// after the #version line. The preprocessor removes the other models' code before the compiler sees it.
// (VK: uber.vk.frag keeps all three in the SPIR-V; the driver folds them away at pipeline creation.)

#ifndef LIGHTING_MODEL
#define LIGHTING_MODEL 0
#endif
#ifndef TOON_DESATURATION
#define TOON_DESATURATION 0.0
#endif

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vViewVec;
layout(location = 4) in vec3 vLightVec;

layout(binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 N = normalize(vNormal);
  vec3 L = normalize(vLightVec);
  vec3 V = normalize(vViewVec);
  vec3 R = reflect(-L, N);
#if LIGHTING_MODEL == 0  // Phong
  vec3 ambient = vColor * 0.25;
  vec3 diffuse = max(dot(N, L), 0.0) * vColor;
  vec3 specular = pow(max(dot(R, V), 0.0), 32.0) * vec3(0.75);
  outColor = vec4(ambient + diffuse * 1.75 + specular, 1.0);
#elif LIGHTING_MODEL == 1  // Toon: quantised diffuse
  float intensity = dot(N, L);
  vec3 color;
  if (intensity > 0.98) color = vColor * 1.5;
  else if (intensity > 0.9) color = vColor * 1.0;
  else if (intensity > 0.5) color = vColor * 0.6;
  else if (intensity > 0.25) color = vColor * 0.4;
  else color = vColor * 0.2;
  color = mix(color, vec3(dot(vec3(0.2126, 0.7152, 0.0722), color)), TOON_DESATURATION);
  outColor = vec4(color, 1.0);
#else  // Textured
  vec3 texel = texture(uTexture, vUv).rgb;
  vec3 ambient = texel * 0.25;
  vec3 diffuse = max(dot(N, L), 0.0) * texel;
  float specular = pow(max(dot(R, V), 0.0), 32.0) * 0.5;
  outColor = vec4(ambient + diffuse + vec3(specular), 1.0);
#endif
}
