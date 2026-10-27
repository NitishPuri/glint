#version 460
#include "glint.glsl"

// Shows the grid fitted into a square, colored by u − v, with the brush outline.

layout(location = 0) in vec2 vNdc;

SAMPLER(0, 0) sampler2D uSim;

PUSH_CONSTANTS Display {
  vec4 view;   // x = screen aspect, y = palette
  vec4 brush;  // xy = cursor (texels), z = radius, w = 1: show
} pc;

layout(location = 0) out vec4 outColor;

vec3 palette(float t, float which) {
  if (which < 0.5) {  // ink: deep blue → teal → white
    return t < 0.5 ? mix(vec3(0.02, 0.03, 0.1), vec3(0.0, 0.5, 0.55), t * 2.0)
                   : mix(vec3(0.0, 0.5, 0.55), vec3(0.95, 0.97, 0.9), t * 2.0 - 1.0);
  }
  return vec3(t);  // grayscale
}

void main() {
  float aspect = pc.view.x;
  vec2 q = aspect > 1.0 ? vec2(vNdc.x * aspect, vNdc.y) : vec2(vNdc.x, vNdc.y / aspect);
  q /= 0.95;
  if (any(greaterThan(abs(q), vec2(1.0)))) {
    outColor = vec4(0.015, 0.015, 0.02, 1.0);
    return;
  }
  vec2 uv = q * 0.5 + 0.5;  // texel row 0 at the bottom, in both APIs (it was written by texel index)
  vec2 uvValue = texture(uSim, uv).xy;
  vec3 c = palette(clamp(uvValue.x - uvValue.y, 0.0, 1.0), pc.view.y);
  if (pc.brush.w > 0.5) {
    vec2 texel = uv * vec2(textureSize(uSim, 0));
    float d = abs(distance(texel, pc.brush.xy) - pc.brush.z);
    c = mix(c, vec3(1.0, 0.6, 0.2), smoothstep(1.5, 0.0, d));
  }
  outColor = vec4(c, 1.0);
}
