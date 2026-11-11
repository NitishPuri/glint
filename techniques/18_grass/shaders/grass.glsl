// 18_grass — what the grass and ground shaders share: the push constants, the wind texture, the wind function and
// the light. Include after glint.glsl.
#ifndef GRASS_GLSL
#define GRASS_GLSL

PUSH_CONSTANTS Draw {
  mat4 viewProjection;
  vec4 eye;    // xyz camera position, w time (s)
  vec4 wind;   // xy direction (world x, z), z strength, w flutter
  vec4 gust;   // x gust scale (m), y gust speed (m/s), z evolve (texture depths/s), w fade distance (m)
  vec4 field;  // x ground half size (m), y view, z blade count
} pc;

// 16's noise texture (64^3 RGBA16F, tiles on all axes, REPEAT sampler). R = fBm in about [0.2, 0.8], mean 0.5.
SAMPLER(0, 0) sampler3D uWind;

// Wind at a point of the ground: xy = horizontal bend (blade heights per unit t²), z = the gust value (for the debug
// view). One call per *blade* (its root), not per vertex: GG1 ch7 §7.4.4 — every vertex of a grass object moves
// with the same wind, so the blade bends without its shape being distorted (§7.4.3's per-vertex version stretches).
vec3 windAt(vec2 xz, float time, float phase) {
  vec2 dir = pc.wind.xy;
  // The gust pattern travels downwind (frozen turbulence, Taylor's hypothesis) and slowly changes shape: time is
  // the texture's z, which wraps, so the animation loops seamlessly after 1/evolve seconds.
  vec2 p = xz - dir * pc.gust.y * time;
  vec3 coord = vec3(p / pc.gust.x, time * pc.gust.z);
  // A vertex shader has no screen-space derivatives, so no automatic mip selection: textureLod says which level.
  // (One level here anyway. GL calls this "vertex texture fetch", a separate capability in the 2004 era.)
  float gust = textureLod(uWind, coord, 0.0).r;
  float across = textureLod(uWind, coord + vec3(0.37, 0.61, 0.5), 0.0).r - 0.5;  // an unrelated second sample
  float strength = pc.wind.z * (0.15 + 2.0 * smoothstep(0.35, 0.75, gust));  // calm troughs, strong gusts
  // Per-blade flutter: a sine with a random phase and rate (the classic animated-grass shader; ch7 leaves its
  // CalcTranslation() open). Stronger when the gust is.
  float flutter = pc.wind.w * sin(time * (7.0 + 4.0 * phase) + phase * 6.2832) * (0.4 + strength);
  vec2 perp = vec2(-dir.y, dir.x);
  return vec3(dir * strength + perp * (across * pc.wind.z * 1.2 + flutter), gust);
}

// Light: a low sun and sky ambient, in linear RGB. kSky is the clear colour (0.62, 0.74, 0.86), sRGB-decoded, so
// fog fades into exactly the background.
const vec3 kSunDir = normalize(vec3(0.45, 0.55, 0.35));
const vec3 kSun = vec3(1.0, 0.9, 0.75) * 2.0;
const vec3 kSky = vec3(0.342, 0.507, 0.711);

vec3 applyFog(vec3 color, vec3 world) {
  float f = 1.0 - exp(-distance(world, pc.eye.xyz) * 0.012);
  return mix(color, kSky, f);
}

#endif
