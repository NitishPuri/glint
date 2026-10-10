# 14_compute_particles

## What it shows
Up to 1M particles (2^10 … 2^20, UI slider) that never leave the GPU. Each frame:
1. A **compute shader** integrates every particle in place in a storage buffer (SSBO):
   - forces: gravity, two orbiting attractors, drag;
   - collision: bounce on the floor;
   - lifecycle: respawn at a fountain emitter when the lifetime runs out, using stateless PCG-hash randomness.
2. The **same buffer** is bound as the vertex buffer of a `POINTS` draw.
3. The points are drawn additively into 12's HDR target, then tone mapped (ACES).

The CPU only sends 48 bytes of constants per frame. Reset / pause / count / force sliders are in the UI.

## Read
- [GG3 ch23 High-Speed, Off-Screen Particles](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)
  — the *drawing* half. When particles are big, soft and overlapping, fill rate dominates, so the chapter
  renders them into a low-resolution off-screen target and upsamples, with care at depth edges.
  - Here the particles are 1-pixel additive points, so it doesn't apply yet; the RADV number below shows where
    the cost goes instead.
  - The chapter becomes relevant once particles grow into sprites (25_light_scattering, 27_fluids_2d smoke).
  - Local CD: — (online only)
- Jarzynski & Olano, *Hash Functions for GPU Rendering* (JCGT 2020): the PCG hash used for per-particle
  randomness.

## GL path
- **Compute program**: a `gl::Program` with a single `.comp` stage (`GL_COMPUTE_SHADER`).
- **Particle buffer**: `glNamedBufferStorage(size, nullptr, 0)`, i.e. no CPU access. It becomes an SSBO by
  `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf)`. SSBO binding points are their own namespace, separate
  from UBOs; `SSBO(set, binding)` in `glint.glsl` gives `layout(std430, binding)`.
- **Dispatch**: `glDispatchCompute(ceil(n / 256), 1, 1)`. The shader returns early for the excess invocations
  of the last group.
- **The barrier**: `glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT)`.
  - This is the one hazard GL does **not** resolve for you. Writes through SSBOs, images and atomics are
    "incoherent".
  - The bit names how the data will be **read next** (as vertex attributes), not how it was written.
    `GL_SHADER_STORAGE_BARRIER_BIT` is the classic wrong guess: it only covers later SSBO accesses.
- **Draw**: a VAO whose binding 0 is the particle buffer (stride 32, two `vec4` attributes), then
  `glDrawArrays(GL_POINTS, 0, n)`.
  - `glEnable(GL_PROGRAM_POINT_SIZE)` so the shader's `gl_PointSize` is used.
  - Blending is `GL_ONE, GL_ONE` into an RGBA16F FBO.

## VK path
- **Compute pipeline, written out raw**: `VkComputePipelineCreateInfo` takes one shader stage and a layout,
  nothing else. There's no vertex input, rasterizer, blend state or attachment formats, and the dispatch
  happens *outside* `vkCmdBeginRendering`.
  - Compute has its own bind point (`VK_PIPELINE_BIND_POINT_COMPUTE`), so binding it leaves the graphics
    pipeline and sets alone.
- **Particle buffer**: `STORAGE_BUFFER | VERTEX_BUFFER` usage declared at creation, `DEVICE_LOCAL`, and a
  `STORAGE_BUFFER` descriptor.
  - There is **one** buffer, not one per frame in flight. Only the GPU touches it, and barriers order frame
    N+1's compute after frame N's draw. Per-frame copies are for data the CPU writes.
- **Queue**: the context now requires a family with graphics **and** compute. The dispatch is recorded in
  the frame's ordinary command buffer. Async compute on a second queue would need semaphores and queue-family
  ownership transfers, which is a later topic.
- **Two buffer barriers per frame** (`VkBufferMemoryBarrier2`, the first buffer barrier in Glint):
  1. *before* the dispatch: `VERTEX_ATTRIBUTE_INPUT | COMPUTE_SHADER` / `SHADER_STORAGE_WRITE` → `COMPUTE_SHADER`
     / `SHADER_STORAGE_READ|WRITE`. This covers last frame's vertex fetch (write-after-read) and last frame's
     compute (write-after-write). GL needs nothing here.
  2. *after* it: `COMPUTE_SHADER` / `SHADER_STORAGE_WRITE` → `VERTEX_ATTRIBUTE_INPUT` / `VERTEX_ATTRIBUTE_READ`.
     This is GL's `glMemoryBarrier`.
- **Draw**: `vkCmdBindVertexBuffers(particles)`, `vkCmdDraw(n)`. `POINT_LIST` topology is pipeline state;
  `GraphicsPipelineDesc::topology` is new.

## Differences that matter
- **Who synchronizes compute output**: GL makes *you* insert `glMemoryBarrier` for incoherent writes, but only
  on the reading side and only by usage class, for all buffers at once. VK makes you name both sides, per
  buffer (or globally), and also the write-after-read side across frames that GL handles itself.
- **Points**: VK always takes `gl_PointSize` (it must be written for point topology), and sizes above 1 need
  the `largePoints` feature. GL ignores `gl_PointSize` unless `GL_PROGRAM_POINT_SIZE` is enabled.
- **Buffer roles**: a GL buffer can be bound as anything at any time. A VK buffer can only be used in the roles
  its `usage` flags declared.
- Workgroup size (`local_size_x = 256`), `gl_GlobalInvocationID`, std430 and `vkCmdDispatch`/`glDispatchCompute`
  are identical. The compute *language* is the same, and only binding and synchronization differ.

## Gotchas hit
- **Sync validation missed the missing barrier.** With both buffer barriers deleted, VK validation (sync
  validation on) reported **0 issues**, and the image still looked right on NVIDIA.
  - The layer's default doesn't model what a *shader* writes through a storage descriptor.
  - Enabling `syncval_shader_accesses_heuristic` (`VkLayerSettingsCreateInfoEXT` in `vk/context.cpp`, now always
    on) reports both hazards: the in-frame read-after-write and the cross-frame write-after-read.
  - 01–13 stay clean with it on.
- A reset must seed **all** 1M particles, not just the active count. Neither API zeroes new buffer memory, so
  raising the count later would otherwise expose garbage.
- Seeding happens in the compute shader (same hash, same integers), so GL and VK start bit-identical. Parity:
  48.7 dB at 120 frames and 57 dB at 30. Over seconds the two drift apart (different compilers, different float rounding,
  mildly chaotic attractors), but the look is the same.
- Uncapped frame rates at ~1 ms per frame advance the simulation by only ~1 ms per frame, so timing runs need
  `--fixed-dt` to reach a steady state. Otherwise you measure mostly unborn particles.

## Numbers
LOC: gl.cpp 148, vk.cpp 229. Parity 57.1 dB (30 frames). Validation clean on both GPUs. GPU at 1280×720,
Debug, 1M particles in steady state:
- **RTX 3060** (NVIDIA 580): GL 0.88 ms, VK 0.95 ms
- **Cezanne iGPU** (RADV, `VK_LOADER_DRIVERS_SELECT='*radeon*'`): VK 8.6 ms. Additive blending of 1M points
  into a 16-byte-per-pixel target, plus 32 MB of read and write per pass, is a lot of bandwidth for shared
  system memory; GG3 ch23's territory.
