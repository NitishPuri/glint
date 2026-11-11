#version 460
#include "glint.glsl"
#include "color.glsl"
#include "grass.glsl"

// Soil with patches of moss: the same noise texture, a static slice of its turbulence channel (G).

layout(location = 0) in vec3 vWorld;

layout(location = 0) out vec4 outColor;

void main() {
  float patches = texture(uWind, vec3(vWorld.xz / 9.0, 0.3)).g;
  vec3 base = mix(vec3(0.06, 0.05, 0.025), vec3(0.045, 0.1, 0.02), smoothstep(0.2, 0.6, patches));
  vec3 color = base * (kSun * kSunDir.y * 0.8 + kSky * 0.5);
  outColor = vec4(linearToSrgb(applyFog(color, vWorld)), 1.0);
}
