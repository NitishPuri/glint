#version 460
#include "glint.glsl"
#include "grass.glsl"

// One blade, run once per instance. Two vertex bindings feed these inputs at different rates:
//   binding 0, per vertex:   location 0        — the blade strip, the same for every blade
//   binding 1, per instance: locations 1 and 2 — this blade's record, the same for all its vertices
// GL: glVertexArrayBindingDivisor(vao, 1, 1). VK: VkVertexInputBindingDescription{1, 32, VK_VERTEX_INPUT_RATE_INSTANCE}.
// The shader can't tell the difference: rates are vertex-input state, not GLSL.

layout(location = 0) in vec2 aStrip;  // x edge (-1 left, +1 right, 0 tip), y height fraction t
layout(location = 1) in vec4 aRoot;   // xyz root, w facing angle
layout(location = 2) in vec4 aShape;  // x height, y width, z lean, w seed

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vData;  // x t, y seed, z gust
layout(location = 3) flat out uint vInstance;

// Root-relative position of the point at height fraction t on a blade of height h bent by b (horizontal, in blade
// heights per unit t²). The offset grows with t², so the root stays put and the tip moves most (GG1 ch7 §7.4: only
// the upper vertices move; this is a smooth version of its "texcoord.v < 0.1" switch). Then each point is pulled
// back to distance t * h from the root, so a bent blade gets lower instead of longer.
vec3 bladeCurve(float t, float h, vec2 b) {
  vec3 chord = vec3(b.x * t, 1.0, b.y * t);  // root -> point is h * (b.x t², t, b.y t²) = h t * chord
  return normalize(chord) * (t * h);
}

void main() {
  float edge = aStrip.x, t = aStrip.y;
  vec3 root = aRoot.xyz;
  float angle = aRoot.w, height = aShape.x, width = aShape.y, lean = aShape.z, seed = aShape.w;
  float time = pc.eye.w;

  // LOD: past half the fade distance, blades drop out in seed order (each shrinks over a short range), so the
  // field thins rather than ending at a line. GG1 ch7 blends far grass objects out with alpha instead — that needs
  // back-to-front sorting; shrinking doesn't. The vertices still run: removing them for real is 19's job.
  float keep = clamp((pc.gust.w - distance(pc.eye.xyz, root)) / (0.5 * pc.gust.w), 0.0, 1.0);
  height *= smoothstep(seed * 0.9, seed * 0.9 + 0.1, keep);

  vec3 wind = windAt(root.xz, time, seed);
  vec2 facing = vec2(-sin(angle), cos(angle));  // blade plane normal, horizontal
  vec2 bend = facing * lean + wind.xy;          // own curl + wind
  vec3 side = vec3(cos(angle), 0.0, sin(angle));

  float halfWidth = 0.5 * width * (1.0 - pow(t, 1.5));  // tapers to the tip
  vWorld = root + bladeCurve(t, height, bend) + side * (edge * halfWidth);

  // Normal = side x tangent of the bent curve (finite difference on the unit-height curve: no NaN when the LOD
  // shrinks h to 0). Tilted towards the edge so the flat blade shades as if rounded.
  vec3 tangent = bladeCurve(min(t + 0.02, 1.0), 1.0, bend) - bladeCurve(max(t - 0.02, 0.0), 1.0, bend);
  vec3 n = normalize(cross(side, tangent));
  vNormal = normalize(n + side * (edge * 0.35));

  vData = vec3(t, seed, wind.z);
  vInstance = uint(INSTANCE_ID);
  gl_Position = pc.viewProjection * vec4(vWorld, 1.0);
}
