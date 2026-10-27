// 17_reaction_diffusion — the Gray-Scott model, shared by the compute and the fragment step (and mirrored line for
// line by rd_cpu.h). Two chemicals per cell, u and v:
//   du/dt = Du ∇²u − u·v² + F·(1 − u)        u is fed in at rate F
//   dv/dt = Dv ∇²v + u·v² − (F + k)·v        v is removed at rate F + k
// GPU Gems 2 ch31 (§31.5) uses the same model with a 5-point Laplacian (rd.cg on the CD); this uses Karl Sims'
// 9-point stencil (center −1, edges 0.2, corners 0.05), which is more isotropic, with Du = 1, Dv = 0.5, dt = 1.
//
// The including shader defines `vec2 fetchSim(ivec2 texel)` first (imageLoad in compute, texelFetch in a fragment
// shader): the only line that differs between the two paths.
#ifndef RD_GLSL
#define RD_GLSL

PUSH_CONSTANTS Step {
  ivec4 size;   // x, y = grid size, z = 1: brush active
  vec4 params;  // x = F, y = k, z = Du, w = Dv
  vec4 brush;   // xy = center (texels), z = radius, w = dt
} pc;

ivec2 wrapTexel(ivec2 p) { return (p + pc.size.xy * 64) % pc.size.xy; }  // a torus: no edges

vec2 fetchSim(ivec2 texel);

vec4 rdStep(ivec2 p) {
  vec2 c = fetchSim(p);
  vec2 lap = -c;
  lap += 0.2 * (fetchSim(wrapTexel(p + ivec2(1, 0))) + fetchSim(wrapTexel(p + ivec2(-1, 0))) +
                fetchSim(wrapTexel(p + ivec2(0, 1))) + fetchSim(wrapTexel(p + ivec2(0, -1))));
  lap += 0.05 * (fetchSim(wrapTexel(p + ivec2(1, 1))) + fetchSim(wrapTexel(p + ivec2(-1, 1))) +
                 fetchSim(wrapTexel(p + ivec2(1, -1))) + fetchSim(wrapTexel(p + ivec2(-1, -1))));
  float u = c.x, v = c.y;
  float uvv = u * v * v;
  float F = pc.params.x, k = pc.params.y, dt = pc.brush.w;
  u += (pc.params.z * lap.x - uvv + F * (1.0 - u)) * dt;
  v += (pc.params.w * lap.y + uvv - (F + k) * v) * dt;
  // The brush (left mouse): drop in some v, like the seeds.
  if (pc.size.z != 0 && distance(vec2(p) + 0.5, pc.brush.xy) < pc.brush.z) {
    u = 0.5;
    v = 0.25;
  }
  return vec4(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0), 0.0, 1.0);
}

#endif
