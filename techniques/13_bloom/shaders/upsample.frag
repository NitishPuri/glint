#version 460
#include "glint.glsl"

// Bloom upsample: smaller level i+1 -> level i, through a 3x3 tent filter, *added* to what level i already
// holds (its downsample result) by additive blending. Going back up the chain, each level ends up as
// "its own blur + all wider blurs", so level 0 holds a wide, smooth glow made only of cheap small filters.

layout(location = 0) in vec2 vUv;

SAMPLER(0, 0) sampler2D uSource;  // level i+1 (one-level view)

PUSH_CONSTANTS Upsample {
  vec4 params;  // x = radius in source texels
} pc;

layout(location = 0) out vec4 outColor;

vec3 tap(vec2 offset) { return texture(uSource, vUv + offset).rgb; }

void main() {
  vec2 r = pc.params.x / vec2(textureSize(uSource, 0));
  vec3 sum = tap(vec2(0)) * 4.0;
  sum += (tap(vec2(0, r.y)) + tap(vec2(-r.x, 0)) + tap(vec2(r.x, 0)) + tap(vec2(0, -r.y))) * 2.0;
  sum += tap(vec2(-r.x, r.y)) + tap(vec2(r.x, r.y)) + tap(vec2(-r.x, -r.y)) + tap(vec2(r.x, -r.y));
  outColor = vec4(sum / 16.0, 1.0);  // blending (ONE, ONE) adds it to the target
}
