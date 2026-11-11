#version 460
#include "glint.glsl"
#include "color.glsl"
#include "grass.glsl"

// Blades are single-sided geometry seen from both sides (culling off): gl_FrontFacing says which side we see, and
// the normal is flipped to face the viewer. Front = counter-clockwise in *both* APIs here, because VK flips Y with a
// negative viewport height, which also flips the winding back to GL's (see docs/gl-vs-vk.md).

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vData;  // x t, y seed, z gust
layout(location = 3) flat in uint vInstance;

layout(location = 0) out vec4 outColor;

vec3 hashColor(uint i) {
  uint h = i * 747796405u + 2891336453u;
  h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
  return vec3(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u) / 255.0;
}

void main() {
  float t = vData.x, seed = vData.y;
  vec3 n = normalize(vNormal);
  vec3 N = gl_FrontFacing ? n : -n;
  vec3 V = normalize(pc.eye.xyz - vWorld);

  // Dark at the root, light at the tip; some blades yellower (GG1 ch7 §7.3.1 varies green and yellow in the
  // texture, for age and ripeness).
  vec3 base = mix(vec3(0.02, 0.06, 0.01), vec3(0.26, 0.45, 0.08), t);
  base *= mix(vec3(0.85, 1.0, 0.8), vec3(1.3, 1.05, 0.55), seed * seed);

  if (pc.field.y > 1.5) {  // instance index: which record of the instance buffer drew this blade
    base = hashColor(vInstance) * mix(0.3, 1.0, t);
  } else if (pc.field.y > 0.5) {  // wind: the fBm value this blade's root sampled
    base = mix(vec3(0.05, 0.1, 0.6), vec3(1.0, 0.25, 0.05), smoothstep(0.3, 0.75, vData.z)) * mix(0.3, 1.0, t);
  }

  float ndl = dot(N, kSunDir);
  vec3 diffuse = base * kSun * max(ndl, 0.0);
  vec3 translucency = base * vec3(1.0, 1.1, 0.5) * kSun * 0.45 * max(-ndl, 0.0);  // sun through the blade
  vec3 h = normalize(kSunDir + V);
  vec3 specular = kSun * 0.12 * t * pow(max(dot(N, h), 0.0), 40.0);
  vec3 ambient = base * kSky * 0.7;
  float occlusion = mix(0.25, 1.0, smoothstep(0.0, 1.0, t));  // the field shades its own lower half

  vec3 color = (ambient + diffuse + translucency) * occlusion + specular;
  outColor = vec4(linearToSrgb(applyFog(color, vWorld)), 1.0);
}
