# 15_prefix_sum

## What it shows
**Stream compaction** built on a **parallel prefix sum (scan)**, entirely in compute shaders, on up to 4M points:
1. **flags**: point i of a jittered W×H grid gets 1 if it lies inside an animated star, else 0.
2. **scan**: the exclusive prefix sum of the flags gives each selected point its slot in the output. The scan
   is multi-level:
   - scan each 512-element block and write its total;
   - scan the totals the same way, recursively;
   - add each block's offset back to its elements.
3. **scatter**: selected point i writes `i` to `compact[1 + slot]`, and the last thread writes the count.
4. **draw**: every point dimmed, then the compacted ones coloured by slot. The bottom-to-top gradient shows the
   scan *keeps the original order*: points are numbered row by row.

The UI switches between two block-scan kernels (**shared memory**, GG3 ch39's Blelloch scan, vs **subgroup
arithmetic**). It also shows the GPU time of each phase from timestamps, and has a **verify on CPU** button
that reads everything back and recomputes the scan and compaction. Every run also verifies both kernels once
at startup and logs the result, plus a per-run `15_prefix_sum timing:` line.

## Read
- [GG3 ch39 Parallel Prefix Sum (Scan) with CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda)
  — the core text.
  - Naive (Hillis-Steele, O(n log n)) vs work-efficient (Blelloch: up-sweep + down-sweep, O(n)). `scan_shared.comp`
    is Example 39-2.
  - Arrays larger than a block (39.2.4): exactly the levels here.
  - Stream compaction is one of the applications (39.3).
  - Not done: bank-conflict padding (39.2.3).
  - CUDA → GLSL: `__shared__` = `shared`, `__syncthreads()` = `barrier()`, thread/block index =
    `gl_LocalInvocationID` / `gl_WorkGroupID`.
  - Local CD: — (online only)
- [GG2 ch36 Stream Reduction Operations for GPGPU Applications](https://developer.nvidia.com/gpugems/gpugems2/part-iv-general-purpose-computation-gpus-primer/chapter-36-stream-reduction)
  — the same problem on pre-compute GPUs: no scatter, so compaction needed a log-step search in a pixel shader.
  It shows what scatter (writing to a computed address) bought. Local CD:
  `GPU-Gems-2-CD-Content/General-Purpose_Computation_on_GPUs_A_Primer/Ch_36_Stream_Reduction_Operations_for_GPGPU_Applications`
- [GG2 ch31 Mapping Computational Concepts to GPUs](https://developer.nvidia.com/gpugems/gpugems2/part-iv-general-purpose-computation-gpus-primer/chapter-31-mapping-computational)
  — the gather/scatter, stream and kernel vocabulary, from when "compute" meant render-to-texture. Local CD:
  `GPU-Gems-2-CD-Content/General-Purpose_Computation_on_GPUs_A_Primer/Ch_31_Mapping_Computational_Concepts_to_GPUs`

## The kernels
- **Shared memory (Blelloch)**: 256 threads, 2 elements each, 512 values in `shared uint temp[512]`. Every tree
  step is `memoryBarrierShared(); barrier();`: all of the workgroup waits, and all of its shared writes become
  visible. That's 19 barriers per block.
- **Subgroups**: `subgroupInclusiveAdd` scans 32 (NVIDIA) or 64 (RADV) lanes in registers with no barriers.
  Then one shared-memory step combines the 8 (or 4) subgroup totals, using a second `subgroupExclusiveAdd` in
  the first subgroup. That's 2 barriers per block.
- Both read and write through the same `load/store/storeBlockSum` (`scan_block.glsl`) and pass the same CPU
  check.

## GL path
- **Five compute programs** (flags, 2 scans, add offsets, scatter). Each dispatch is followed by
  `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`, because the next pass reads through an SSBO. After the
  scatter it's the same bit *again*, because the draw's vertex shader reads `compact` as an SSBO. Compare 14,
  where the draw read the buffer as vertex attributes and needed `GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT`.
- **Subgroups** are an extension, `GL_KHR_shader_subgroup`. Its tokens aren't in our glad, so they're
  defined in `gl.cpp`. `glGetIntegerv` gives the size (32), stages and features; the subgroup program is only
  built if compute + arithmetic are supported.
- **Timestamps**: `glQueryCounter(id, GL_TIMESTAMP)` at 4 points, in a ring of 4 sets. Each set is read only
  when `GL_QUERY_RESULT_AVAILABLE` says so, so the CPU never waits.
- **CPU check**: `glCopyNamedBufferSubData` into a `GL_MAP_READ_BIT | GL_CLIENT_STORAGE_BIT` buffer, then
  `glMapNamedBufferRange`. The barrier before the copy is `GL_BUFFER_UPDATE_BARRIER_BIT`, which covers
  get/copy/sub-data reads of shader-written buffers.

## VK path
- **Five compute pipelines** through the new `vk::createComputePipeline` helper (14 wrote one out raw). They
  share one pipeline layout and one descriptor set holding all three storage buffers. That set is bound
  **twice**: at the compute bind point and again at the graphics bind point for the draw.
- **Global memory barriers** (`VkMemoryBarrier2`, every buffer and image) between passes, like
  `glMemoryBarrier`. 14 used a per-buffer barrier; drivers mostly treat both the same, and the *stages* are
  what costs.
  - At frame start, one barrier orders last frame's vertex reads, copies and compute writes before this
    frame's compute.
- **Subgroups** are core since 1.1, with nothing to enable. `VkPhysicalDeviceSubgroupProperties` reports
  the size (32 on NVIDIA, 64 on RADV), `supportedStages` and `supportedOperations`.
- **Timestamps**: a `VK_QUERY_TYPE_TIMESTAMP` pool with 4 queries per frame slot.
  - `vkCmdResetQueryPool` is recorded first, then `vkCmdWriteTimestamp2(ALL_COMMANDS)`.
  - Results are read in the next `record()` of that slot (its fence has signalled), with no WAIT flag.
  - Ticks × `timestampPeriod` = ns.
- **CPU check**: `vkCmdCopyBuffer` into a `HOST_VISIBLE | HOST_COHERENT | HOST_CACHED` buffer, then a
  `COPY → HOST` barrier. The next frame does `vkDeviceWaitIdle` and reads the mapping.

## Differences that matter
- **Barrier granularity**: both APIs here use "all buffers" barriers. GL can't do anything narrower; VK can
  (14), but needn't.
- **What GL infers vs what VK states**: GL needs barriers only before *reads* of incoherent writes. VK also
  needs the write-after-read ordering against the previous frame, and against copies.
- **Subgroups**: an extension and its queries in GL, core properties in VK. The GLSL
  (`GL_KHR_shader_subgroup_*`) is identical, and so are the results.
- **Readback**: the same staging pattern in both. GL *can* read device buffers directly
  (`glGetNamedBufferSubData`), but NVIDIA then moves those buffers to host memory and says so (see Gotchas).
- **Timestamps**: the same idea. GL's queries are objects you poll; VK's are a pool you reset in the command
  buffer and read once the fence says the frame is done.

## Gotchas hit
- **NVIDIA migrated the SSBOs to system memory** after the first direct `glGetNamedBufferSubData`, logging one
  performance warning per buffer (severity medium, so the run failed the clean-log rule). Staging through a
  client-storage buffer fixed it and mirrors the VK code.
- **Writable storage buffers in a vertex shader** count as vertex-stage stores in VK, which need
  `vertexPipelineStoresAndAtomics`. So the draw's vertex shader includes the shared buffer declarations with
  `#define BUFFER_ACCESS readonly`.
- **The CPU doesn't know the count**: it's on the GPU. Mode 1 draws N vertices, and those past `compact[0]` clip
  themselves. Indirect draws (18_gpu_culling) take the count from the buffer.
- GL and VK pick identical points (same counts in the startup check). Parity is lower than usual (36 dB) because
  1-pixel points over a 1-pixel grid are sensitive to point-rasterization details.

## Numbers
LOC: gl.cpp 226, vk.cpp 305. Parity 36.1 dB. Both kernels verified on NVIDIA (GL + VK, subgroup 32) and on
RADV (VK, subgroup 64). Validation is clean.

4M points (1280×720, Debug):

| RTX 3060 | flags | scan | scatter |
|---|---|---|---|
| GL, shared memory | 0.12 ms | 0.41 ms (10.1 G/s) | 0.13 ms |
| VK, shared memory | 0.15 ms | 0.35 ms (12.0 G/s) | 0.13 ms |
| GL, subgroups | 0.12 ms | 0.25 ms (17.1 G/s) | 0.13 ms |
| VK, subgroups | 0.13 ms | 0.26 ms (16.1 G/s) | 0.14 ms |

- Subgroups are ~1.5× faster.
- The subgroup scan moves ~64 MB (block scan read + write, then add-offsets read + write of 16 MB each), so
  ~250 GB/s against the card's ~336 GB/s: close to bandwidth-bound.
- The next step would be a single-pass scan ("decoupled look-back", Merrill & Garland 2016), which reads and
  writes each element once.
