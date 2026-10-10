# glint — Migration & Integration Plan

Goal: merge `Glint_gl` (OpenGL) and `Glint_vk` (Vulkan) into one repo where each rendering technique
exists in both APIs, side by side, sharing only API-agnostic code. See `../CLAUDE.md` for the rules.

Work top to bottom. Each phase ends with a **Done when** check; don't start the next phase until it holds.
Tick boxes as work lands (commit the tick with the work).

Legend: `GL:` = source in `/mnt/e/tree/graphics/Glint_gl`, `VK:` = source in `/mnt/e/tree/graphics/Glint_vk`.

---

## Phase 0 — Machine setup (owner runs these)

- [x] `sudo apt install cmake ninja-build pkg-config libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`
      (X11 dev headers are needed because GLFW is built from source via FetchContent)
- [x] LunarG Vulkan SDK (Linux tarball) → `~/vulkan-sdk/`; add `source ~/vulkan-sdk/*/setup-env.sh` to `~/.bashrc`
- [x] Verify: `glslc --version`, `vulkaninfo --summary` shows RADV, `vkconfig` launches
- [x] RenderDoc installed (tarball from renderdoc.org → `~/tools/renderdoc`)

**Done when:** the four checks above pass.

---

## Phase 1 — Skeleton & build system

- [x] `CMakeLists.txt` (root) + `cmake/deps.cmake` with FetchContent:
      glfw 3.4, glm 1.0.1, imgui (docking branch, pinned tag), fmt 11.x, stb, tinyobjloader
      → pinned: imgui v1.92.9b-docking, fmt 11.2.0, stb @2c980bb, tinyobjloader v2.0.0rc13 (archives + SHA256)
- [x] `find_package(Vulkan REQUIRED)` + `find_program(Vulkan_GLSLC_EXECUTABLE glslc ...)` (CMake 3.22 has no
      `COMPONENTS glslc`, that's 3.24+); `find_package(OpenGL REQUIRED)`
- [x] Generate glad2 for **GL 4.6 core** + `GL_KHR_debug` → `external/glad/` (commit generated files)
- [x] `cmake/shaders.cmake`: function `glint_compile_shaders(target DIR)` → glslc `*.vk.{vert,frag,comp}` → `build/<cfg>/shaders/<technique>/*.spv`, with `-g` in Debug
- [x] Targets: `glint_core` (static lib), `glint_gl_backend`, `glint_vk_backend`, `glint_gl` (exe), `glint_vk` (exe)
- [x] Compile definitions: `GLINT_ASSET_DIR`, `GLINT_SHADER_DIR` (source dir for GL, build dir for SPIR-V)
- [x] Warnings: `-Wall -Wextra -Wpedantic`; `_DEBUG` on Debug for non-MSVC; export `compile_commands.json`
- [x] Copy assets: `GL:res/*` + `VK:res/*` → `assets/` (dedupe; note origin in `assets/README.md`)
- [x] `README.md`, `build.sh`, `run.sh <gl|vk> [technique]`

**Done when:** empty `glint_gl` and `glint_vk` executables build from a clean clone with one cmake command.

---

## Phase 2 — `core/` (API-agnostic)

Pull the best of both, strip every GL/VK type.

| Piece | Take from | Notes |
|---|---|---|
| `window` | VK:`core/window.*` + GL:`Core/Window.h` | ctor takes `enum class GraphicsApi {OpenGL, Vulkan}`; sets `GLFW_CLIENT_API` / context hints accordingly; resize + framebuffer callbacks; no GL calls inside |
| `input` | VK:`core/input_.h`, GL: scene key handling | key/mouse state, per-frame deltas |
| `camera` | VK:`core/camera.*` (arcball) | also keep a fly-camera mode from GL:`Graphics/Camera.*`; projection takes a `ClipDepth {NegOneToOne, ZeroToOne}` flag |
| `log` | GL:`Core/Logger.h` | rewrite on fmt; drop the std::format shim |
| `timer` | GL:`Core/ScopedTimer.h` | CPU timer only; GPU timers are per-backend (Phase 7) |
| `assets` | GL:`Graphics/Mesh.cpp`, `Core/VBOIndex.cpp`, VK:`renderer/mesh_factory.cpp` | `MeshData loadObj(path)`, `MeshData makeCube()/makeQuad()`, `indexMesh()`, `computeTangents()`, `ImageData loadImage(path)` — pure CPU |
| `ui` | GL:`Core/GuiLayer.h`, VK:`glint_ui/` | only ImGui *frame logic + panels* (technique picker, stats, params); backend init lives in each app |
| `technique registry` | GL:`Core/SceneManager.h`, VK:`samples/sample_manager.*` | template `Registry<T>` keyed by name, instantiated separately per backend |
| `config` | VK:`core/config.*` | paths, window size, vsync, validation on/off, via CLI flags |

- [x] Each piece above ported and building
- [x] Unit tests (doctest, FetchContent) for `assets` (cube vertex count, tangent orthogonality, index dedupe) and `camera` (projection clip-depth variants)

**Done when:** `glint_core` builds with zero GL/VK includes (`grep -r "vulkan\|glad" src/core` is empty) and tests pass.

---

## Phase 3 — GL backend + GL app

- [x] `app_gl/main.cpp`: window(OpenGL) → glad load → KHR_debug callback (from GL:`Core/Sysinfo.cpp`) → ImGui GLFW+OpenGL3 backends → registry → loop
- [x] Port **01_triangle** raw in `techniques/01_triangle/gl.cpp` (no helpers yet)
- [x] Introduce `gl/` helpers by extracting from GL:`Graphics/` — `Shader` (with hot reload), `Buffer`, `VertexArray`, `Texture`, `Framebuffer`.
      Each one only after a technique has done it raw (CLAUDE.md): **done:** `Program` (shader.h, hot reload), `Buffer`,
      `VertexArray`, `Mesh` (after 01), `Texture` + `Sampler` (after 03), `Framebuffer` (after 07). Switch to **DSA** (`glCreateBuffers`, `glNamedBufferStorage`, `glTextureStorage2D`…) — GL 4.5+ DSA is the GL idiom closest to Vulkan's explicit objects; note it in `gl-vs-vk.md`
- [x] Port GL scenes as techniques (shaders move to `techniques/<n>/shaders/*.gl.*`).
      Use `Glint_gl/docs/scenes.md` for what each scene does and fix everything in
      `Glint_gl/docs/known-issues.md` on the way (don't port the bugs):
  - [x] 02_cube ← `Scenes/CubeScene.h`
  - [x] 03_textured_cube ← `Scenes/UVCubeScene.h`
  - [x] 04_basic_shading ← `Scenes/BasicShading.h`
  - [x] 05_indexed_mesh ← `Scenes/VBOIndexing.h`
  - [x] 06_normal_mapping ← `Scenes/NormalMapping.h`
  - [x] 07_render_to_texture ← `Scenes/RenderToTexture.h`
  - [x] 08_shadow_mapping ← `Scenes/ShadowMapping.h`
  (`QuadScene` becomes part of 01/07.)
- [x] Each technique gets `params.h` + shared ImGui panel (standard shading shared via `techniques/common/`)

**Done when:** `glint_gl` runs all 8 techniques on RADV with no KHR_debug errors, matching the old Glint_gl visually.

---

## Phase 4 — Vulkan foundation + VK app

Port from Glint_vk, but modernise. Read VK:`src/minimal/triangle/main.cpp` first — that's the raw reference.

- [x] `vk/context` ← VK:`renderer/vk_context.*`: instance (1.3), debug utils messenger, surface, physical device pick (prefer discrete > integrated, **skip llvmpipe/CPU**), queues, enable `dynamicRendering` + `synchronization2` features
- [x] `vk/swapchain` ← VK:`renderer/swapchain.*`: recreate on resize/`VK_ERROR_OUT_OF_DATE_KHR`
- [x] `vk/frame` ← VK:`synchronization_manager.*` + `command_manager.*`: **frames in flight = 2**, per-frame command pool/buffer + fence + acquire semaphore; **present semaphore per swapchain image**. Record command buffers every frame (drop Glint_vk's cached command buffers — ImGui needs per-frame recording anyway, see VK commit 2be473a)
- [x] Transitions via `vkCmdPipelineBarrier2`; small `transitionImage()` helper
- [x] `vk/debug`: object names + `vkCmdBeginDebugUtilsLabelEXT` per technique pass (shows up in RenderDoc)
- [x] ImGui Vulkan backend with dynamic rendering (`UseDynamicRendering = true`)
- [x] **01_triangle** raw in `techniques/01_triangle/vk.cpp`: pipeline with `VkPipelineRenderingCreateInfo`.
      Changed from "hardcoded vertices in shader": GL 01 uses vertex + index buffers (and the quad toggle), so VK 01
      mirrors it with host-visible buffers + raw `vkAllocateMemory`; staging into DEVICE_LOCAL stays in 02.
- [x] `NOTES.md` for 01 written — the "why does VK need 10× the code" entry

**Done when:** `glint_vk` shows triangle + ImGui, resizes without errors, **zero validation messages**, clean shutdown (no leaked objects reported by validation).

---

## Phase 5 — VK techniques (match the GL set)

Same order as Phase 3. For each: write `vk.cpp` raw-ish, reuse `vk/` helpers only for things already done raw in an earlier technique; then write `NOTES.md`.

- [x] 02_cube — vertex/index buffers, staging upload, raw `vkAllocateMemory`; UBO + descriptor set; depth image; `ClipDepth::ZeroToOne` projection + negative viewport. Introduce `vk/buffer` helper after this.
      ← VK:`samples/cube_sample.*`, `minimal/cube/main.cpp`
- [x] 03_textured_cube — image upload, layout transitions, sampler, combined image sampler descriptors; mipmaps via `vkCmdBlitImage`
      ← VK:`samples/textured_quad.*`, `renderer/texture.*`
- [x] 04_basic_shading — same std140 UBO as GL (push constants appear in 01 and 08 instead)
- [x] 05_indexed_mesh — OBJ via `core/assets`, same mesh data both sides; dynamic UBO offsets for the two draws
- [x] 06_normal_mapping — 3 textures, descriptor set layout with multiple bindings
- [x] 07_render_to_texture — offscreen color+depth image, two dynamic-rendering passes, barrier between them (vs GL FBO + implicit sync)
- [x] 08_shadow_mapping — depth-only pass, depth bias (kept in the shader on both sides for parity; `vkCmdSetDepthBias`/`glPolygonOffset` noted), comparison sampler (`sampler2DShadow`), PCF
- [x] ~~Introduce **VMA** here~~ → **moved to Phase 8** (2026-09-30): raw allocation behind `vk::Buffer`/`vk::Image`
      never got tedious (≤ ~10 allocations per technique). The memory types/heaps entry for RADV is in 02's NOTES.

**Done when:** all 8 techniques run on both backends, validation clean, each has `NOTES.md`.

---

## Phase 6 — Bring over Glint_vk-only samples (give them GL twins)

- [x] 09_dynamic_uniform_buffer ← VK:`samples/dynamic_uniform_buffer.*` | GL twin: `glBindBufferRange` with UBO offset alignment
      (05's VK path already uses `UNIFORM_BUFFER_DYNAMIC` for its two draws — 09 generalises it to many objects)
- [x] 10_specialization_constants ← VK:`samples/specialization_constants.*` | GL twin: `#define` injection at compile time (+ ARB_gl_spirv specialization in Phase 8)
- [x] 11_gltf ← VK:`vks/vk_gltf_model.*` (Sascha Willems) — rewrite loader into `core/assets` (tinygltf → `MeshData` + materials), then both backends render it
- [x] Retire Glint_vk's `vks/VulkanDevice.*` — not needed once `vk/context` exists (nothing ported; `vk/context` covers it)

**Done when:** everything valuable from both old repos exists in glint. Old repos can be archived.

---

## Phase 7 — Comparison tooling (the payoff)

- [x] `--screenshot <technique> <out.png>` in both apps (GL `glReadPixels`, VK copy swapchain/offscreen image → host buffer)
      — done early (Phase 3/4) as `glint_<api> <technique> --screenshot out.png [--frames N]`
- [x] `tools/parity.py`: runs both apps per technique, diffs images, reports PSNR — catches convention bugs (Y flip, depth range, sRGB)
- [x] GPU timings: GL `GL_TIME_ELAPSED` queries vs VK timestamp queries (`timestampPeriod`), shown in the same ImGui stats panel
- [x] CPU frame time + ~~draw-call count~~ **pipeline statistics** (vertices, primitives, VS/FS invocations) side by side —
      counted by the GPU in both APIs, no per-call instrumentation of raw draws needed
- [x] RenderDoc in-app capture button (`renderdoc_app.h`) for both apps — `--renderdoc`

**Done when:** `tools/parity.py` passes for all techniques and the stats panel shows GPU ms for both APIs.

---

## Learning path: Phases 8–15 (towards GPU Gems)

Agreed 2026-10-02. Mirrors the vault's *Glint - Learning Path* note, which has one note per stage with
reading lists and learnings. Order rules: each technique adds **at most one new API concept and one new
rendering idea**, and a GPU Gems chapter is scheduled only after the techniques it depends on. A
technique is done when it has a `NOTES.md` **with a Read section** (template below), passes
`tools/parity.py`, is validation clean, and records GPU ms.

Reading: `GGn chN` = GPU Gems volume/chapter (online text linked); local CD code/shaders/demos are under
`/mnt/e/tree/graphics/nvidia/GPU-Gems/` (path relative to that root, "—" = not on the CD).

## Phase 8 — Linear HDR

Baseline every later effect assumes: one shader source for both APIs, linear lighting in HDR, tone mapping, then the first multi-pass post effect.

- [x] **Shared shaders** — new API: one GLSL source per stage, `#ifdef VULKAN` binding macro; new idea: shader variants from one source
- [x] **12_hdr_tonemapping** — new API: float (RGBA16F) + sRGB formats; GL `GL_FRAMEBUFFER_SRGB` vs VK `_SRGB` swapchain; new idea: linear lighting, exposure, tone mapping
      - Read: [GG3 ch24 The Importance of Being Linear](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear) — the whole chapter: why gamma-space lighting is wrong, sRGB textures and framebuffers; local: —
- [x] **13_bloom** — new API: rendering into individual mip levels; long chains of passes and barriers; new idea: glow, color grading LUTs
      - Read: [GG1 ch21 Real-Time Glow](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-21-real-time-glow) — the glow pipeline (bright pass, separable blur, composite); local: —
      - Read: [GG1 ch22 Color Controls](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-22-color-controls) — color controls: the operations a grading LUT bakes in; local: `GPU-Gems-1-CD-Content/Image_Processing/Color_Controls` — readme only
      - Read: [GG2 ch24 Using Lookup Tables to Accelerate Color Transformations](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color) — 3D lookup tables for color transforms; local: —

## Phase 9 — Compute

Compute shaders and the data-parallel building blocks every simulation and GPU-driven technique later needs.

- [x] **14_compute_particles** — new API: compute dispatch, SSBOs, compute -> vertex barriers (`glMemoryBarrier` vs `vkCmdPipelineBarrier2`); new idea: GPU simulation loop
      - Read: [GG3 ch23 High-Speed, Off-Screen Particles](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles) — rendering many particles cheaply (off-screen, low-res) - for the drawing half; local: —
- [ ] **15_prefix_sum** — new API: shared memory, subgroup operations, timing a compute pass; new idea: scan, stream compaction
      - Read: [GG3 ch39 Parallel Prefix Sum (Scan) with CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda) — the work-efficient scan algorithm; ignore the CUDA syntax, the structure maps 1:1 to compute shaders; local: —
      - Read: [GG2 ch36 Stream Reduction Operations for GPGPU Applications](https://developer.nvidia.com/gpugems/gpugems2/part-iv-general-purpose-computation-gpus-primer/chapter-36-stream-reduction) — stream reduction/compaction on the pre-compute GPU (local source) - why scan matters; local: `GPU-Gems-2-CD-Content/General-Purpose_Computation_on_GPUs_A_Primer/Ch_36_Stream_Reduction_Operations_for_GPGPU_Applications` — large C++ source
      - Read: [GG2 ch31 Mapping Computational Concepts to GPUs](https://developer.nvidia.com/gpugems/gpugems2/part-iv-general-purpose-computation-gpus-primer/chapter-31-mapping-computational) — mapping computational concepts to GPUs - the mental model; local: `GPU-Gems-2-CD-Content/General-Purpose_Computation_on_GPUs_A_Primer/Ch_31_Mapping_Computational_Concepts_to_GPUs` — source
- [ ] **16_noise** — new API: storage images (`imageStore`; VK `STORAGE_IMAGE` + `GENERAL` layout), 3D textures; new idea: procedural noise
      - Read: [GG1 ch5 Implementing Improved Perlin Noise](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise) — Ken Perlin's improved noise, explained by its author; local: —
      - Read: [GG2 ch26 Implementing Improved Perlin Noise](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise) — the shader implementation (local .fx); local: `GPU-Gems-2-CD-Content/High-Quality_Rendering/Ch_26_Implementing_Improved_Perlin_Noise` — .fx shaders

- [ ] **VMA** (moved here from Phase 5/8): swap `vk::Buffer`/`vk::Image` internals once compute
      buffers multiply; NOTES on what it decides (memory type, sub-allocation, dedicated allocations).

## Phase 10 — Geometry throughput

Drawing a lot: instancing, GPU-driven culling with indirect draws, tessellated terrain.

- [ ] **17_grass** — new API: instancing (`glVertexAttribDivisor` vs `VK_VERTEX_INPUT_RATE_INSTANCE`); new idea: procedural vegetation + wind
      - Read: [GG1 ch7 Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass) — **the target chapter**: blade geometry, density, wind animation; local: `GPU-Gems-1-CD-Content/Natural_Effects/Grass` — demo only (zip)
      - Read: [GG3 ch6 GPU-Generated Procedural Wind Animations for Trees](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees) — GPU-generated procedural wind (local source); local: `GPU-Gems-3-CD-Content/content/06` — C++ source
      - Read: [GG3 ch16 Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis) — vegetation animation in Crysis (for ideas); local: `GPU-Gems-3-CD-Content/content/16` — video only
- [ ] **18_gpu_culling** — new API: indirect draws (`glMultiDrawElementsIndirect` vs `vkCmdDrawIndexedIndirectCount`), occlusion queries; new idea: GPU-driven rendering
      - Read: [GG1 ch29 Efficient Occlusion Culling](https://developer.nvidia.com/gpugems/gpugems/part-v-performance-and-practicalities/chapter-29-efficient-occlusion-culling) — occlusion culling fundamentals; local: —
      - Read: [GG2 ch6 Hardware Occlusion Queries Made Useful](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-6-hardware-occlusion-queries-made-useful) — hardware occlusion queries made useful (local source); local: `GPU-Gems-2-CD-Content/Geometric_Complexity/Ch_06_Hardware_Occlusion_Queries_Made_Useful` — C++ source
- [ ] **19_terrain** — new API: tessellation control/evaluation shaders (both APIs); new idea: distance-based LOD terrain
      - Read: [GG2 ch2 Terrain Rendering Using GPU-Based Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry) — geometry clipmaps (local shaders) - we do a simplified, tessellation-based version; local: `GPU-Gems-2-CD-Content/Geometric_Complexity/Ch_02_Terrain_Rendering_using_GPU-Based_Geometry_Clipmaps` — .fx shaders

## Phase 11 — Lighting toolbox

Cubemaps and image-based lighting, deferred shading, ambient occlusion, better shadows.

- [ ] **20_environment** — new API: cubemaps, render-to-cubemap, prefiltering in compute; new idea: image-based lighting
      - Read: [GG1 ch19 Image-Based Lighting](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-19-image-based-lighting) — **target**: image-based lighting; local: `GPU-Gems-1-CD-Content/Materials/Image_Based_Lighting` — FX Composer project only
      - Read: [GG2 ch10 Real-Time Computation of Dynamic Irradiance Environment Maps](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-10-real-time-computation-dynamic) — irradiance environment maps in real time (local source); local: `GPU-Gems-2-CD-Content/Shading_Lighting_and_Shadows/Ch_10_Real-Time_Computation_of_Dynamic_Irradiance_Environment_Maps` — C++ + .fx source, cubemaps
      - Read: [GG3 ch20 GPU-Based Importance Sampling](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling) — GPU importance sampling for glossy reflections (local source); local: `GPU-Gems-3-CD-Content/content/20` — C++ + Cg source
- [ ] **21_deferred** — new API: multiple render targets (MRT); new idea: deferred shading, many lights
      - Read: [GG2 ch9 Deferred Shading in S.T.A.L.K.E.R.](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-9-deferred-shading-stalker) — **target**: deferred shading in S.T.A.L.K.E.R.; local: —
      - Read: [GG3 ch19 Deferred Shading in Tabula Rasa](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-19-deferred-shading-tabula-rasa) — deferred shading in Tabula Rasa (what changed three years later); local: —
- [ ] **22_ssao** — new API: (reuse of the G-buffer); new idea: screen-space ambient occlusion
      - Read: [GG3 ch12 High-Quality Ambient Occlusion](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-12-high-quality-ambient-occlusion) — **target**: high-quality AO (local GLSL); local: `GPU-Gems-3-CD-Content/content/12` — C++ + **GLSL** source
      - Read: [GG1 ch17 Ambient Occlusion](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-17-ambient-occlusion) — classic AO background; local: —
      - Read: [GG2 ch14 Dynamic Ambient Occlusion and Indirect Lighting](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-14-dynamic-ambient-occlusion-and) — dynamic AO and indirect lighting (local source); local: `GPU-Gems-2-CD-Content/Shading_Lighting_and_Shadows/Ch_14_Dynamic_Ambient_Occlusion_and_Indirect_Lighting` — C++ source + models
- [ ] **23_shadows_plus** — new API: layered rendering / array textures; new idea: cascades, omni and soft shadows
      - Read: [GG1 ch11 Shadow Map Antialiasing](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing) — shadow map antialiasing (PCF) - also the theory behind 08; local: —
      - Read: [GG1 ch12 Omnidirectional Shadow Mapping](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-12-omnidirectional-shadow-mapping) — omnidirectional (cube) shadow maps (local shaders); local: `GPU-Gems-1-CD-Content/Lighting_and_Shadows/Omni_Shadow_Mapping` — .fx shaders + scene
      - Read: [GG3 ch10 Parallel-Split Shadow Maps on Programmable GPUs](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus) — **target**: parallel-split (cascaded) shadow maps (local GLSL); local: `GPU-Gems-3-CD-Content/content/10` — C++ + **GLSL** source
      - Read: [GG3 ch8 Summed-Area Variance Shadow Maps](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps) — summed-area variance shadow maps (local source; uses 15's scan); local: `GPU-Gems-3-CD-Content/content/08` — C++ + .fx source
      - Read: [GG2 ch17 Efficient Soft-Edged Shadows Using Pixel Shader Branching](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using) — soft-edged shadows with branching (local demo); local: `GPU-Gems-2-CD-Content/Shading_Lighting_and_Shadows/Ch_17_Efficient_Soft-Edged_Shadows_Using_Pixel_Shader_Branching` — zipped demo

## Phase 12 — Natural effects

The first wave of GPU Gems chapters proper: water, light scattering, cinematic post effects.

- [ ] **24_water** — new API: (reuse: noise, terrain, environment); new idea: Gerstner waves, reflections, caustics
      - Read: [GG1 ch1 Effective Water Simulation from Physical Models](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) — **target**: effective water simulation (local source); local: `GPU-Gems-1-CD-Content/Natural_Effects/Water_Simulation` — C++ + .fx source
      - Read: [GG1 ch2 Rendering Water Caustics](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics) — rendering water caustics (local source); local: `GPU-Gems-1-CD-Content/Natural_Effects/Caustics` — C++ source
      - Read: [GG2 ch18 Using Vertex Texture Displacement for Realistic Water Rendering](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-18-using-vertex-texture-displacement) — vertex texture displacement for water (local source); local: `GPU-Gems-2-CD-Content/Shading_Lighting_and_Shadows/Ch_18_Using_Vertex_Texture_Displacement_for_Realistic_Water_Rendering` — C++ + Cg source, height data
- [ ] **25_light_scattering** — new API: (reuse: HDR, post chain); new idea: god rays, atmosphere
      - Read: [GG3 ch13 Volumetric Light Scattering as a Post-Process](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process) — **target**: volumetric light scattering as a post-process; local: `GPU-Gems-3-CD-Content/content/13` — demo .exe + movie only
      - Read: [GG2 ch16 Accurate Atmospheric Scattering](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering) — accurate atmospheric scattering (local **GLSL**); local: `GPU-Gems-2-CD-Content/Shading_Lighting_and_Shadows/Ch_16_Accurate_Atmospheric_Scattering` — C++ + **GLSL** source
- [ ] **26_post_stack** — new API: (reuse: depth, velocity buffers); new idea: motion blur, depth of field
      - Read: [GG3 ch27 Motion Blur as a Post-Processing Effect](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-27-motion-blur-post-processing-effect) — motion blur as a post-process; local: —
      - Read: [GG3 ch28 Practical Post-Process Depth of Field](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field) — practical post-process depth of field; local: —
      - Read: [GG1 ch23 Depth of Field: A Survey of Techniques](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-23-depth-field-survey-techniques) — depth of field survey; local: `GPU-Gems-1-CD-Content/Image_Processing/Depth_of_Field` — demo .exe only

## Phase 13 — Simulation

GPU simulation, the bridge to [[Physics Engine - Glide]].

- [ ] **27_fluids_2d** — new API: (reuse: compute, storage images); new idea: Stable Fluids
      - Read: [GG1 ch38 Fast Fluid Dynamics Simulation on the GPU](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu) — **target**: fast fluid dynamics (local full source: Flo); local: `GPU-Gems-1-CD-Content/Beyond_Triangles/Fluids` — full Cg + C++ source (Flo)
- [ ] **28_nbody_boids** — new API: (reuse: compute, scan); GPU sort; new idea: N-body, boids, spatial hashing
      - Read: [GG3 ch31 Fast N-Body Simulation with CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-31-fast-n-body-simulation-cuda) — **target**: fast N-body with shared-memory tiling (local CUDA); local: `GPU-Gems-3-CD-Content/content/31` — CUDA + C++ source
      - Read: [GG2 ch46 Improved GPU Sorting](https://developer.nvidia.com/gpugems/gpugems2/part-vi-simulation-and-numerical-algorithms/chapter-46-improved-gpu-sorting) — improved GPU sorting (local source); local: `GPU-Gems-2-CD-Content/Simulation_and_Numerical_Algorithms/Ch_46_Improved_GPU_Sorting` — C++ + shader source
      - Read: [GG3 ch32 Broad-Phase Collision Detection with CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-32-broad-phase-collision-detection-cuda) — broad-phase collision detection (for the spatial hash); local: —
- [ ] **29_sph_rigid** — new API: (reuse); new idea: SPH fluid, particle-based rigid bodies
      - Read: [GG3 ch29 Real-Time Rigid Body Simulation on GPUs](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-29-real-time-rigid-body-simulation-gpus) — real-time rigid body simulation on GPUs; local: `GPU-Gems-3-CD-Content/content/29` — demo binaries only
      - Read: [GG3 ch32 Broad-Phase Collision Detection with CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-32-broad-phase-collision-detection-cuda) — broad phase; local: —
      - Read: [GG3 ch33 LCP Algorithms for Collision Detection Using CUDA](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-33-lcp-algorithms-collision-detection-using-cuda) — LCP algorithms for collision detection (advanced); local: —
- [ ] **30_fluids_3d** — new API: (reuse: 3D textures); new idea: 3D fluid + volume rendering
      - Read: [GG3 ch30 Real-Time Simulation and Rendering of 3D Fluids](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids) — real-time simulation and rendering of 3D fluids; local: `GPU-Gems-3-CD-Content/content/30` — zipped demo + video
      - Read: [GG1 ch39 Volume Rendering Techniques](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-39-volume-rendering-techniques) — volume rendering techniques; local: —

## Phase 14 — Procedural geometry

Geometry generated on the GPU: marching cubes, relief mapping, signed distance fields.

- [ ] **31_marching_cubes** — new API: (reuse: compaction + indirect draws); new idea: procedural terrain from 3D noise
      - Read: [GG3 ch1 Generating Complex Procedural Terrains Using the GPU](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) — **target**: procedural terrains (local full source - geometry shaders; we use compute); local: `GPU-Gems-3-CD-Content/content/01` — full demo source: **geometry-shader marching cubes** (.vsh/.gsh/.psh)
- [ ] **32_relief_mapping** — new API: (reuse); new idea: per-pixel displacement
      - Read: [GG2 ch8 Per-Pixel Displacement Mapping with Distance Functions](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-8-pixel-displacement-mapping-distance-functions) — per-pixel displacement with distance functions (local source); local: `GPU-Gems-2-CD-Content/Geometric_Complexity/Ch_08_Per-Pixel_Displacement_Mapping_with_Distance_Functions` — C++ source + textures
      - Read: [GG3 ch18 Relaxed Cone Stepping for Relief Mapping](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-18-relaxed-cone-stepping-relief-mapping) — relaxed cone stepping for relief mapping (local source); local: `GPU-Gems-3-CD-Content/content/18` — C++ + .fx source
- [ ] **33_sdf_raymarching** — new API: (reuse); new idea: SDF scenes, soft shadows, AO
      - Read: [GG2 ch8 Per-Pixel Displacement Mapping with Distance Functions](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-8-pixel-displacement-mapping-distance-functions) — distance functions; local: `GPU-Gems-2-CD-Content/Geometric_Complexity/Ch_08_Per-Pixel_Displacement_Mapping_with_Distance_Functions` — C++ source + textures
      - Read: [GG3 ch34 Signed Distance Fields Using Single-Pass GPU Scan Conversion of Tetrahedra](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-34-signed-distance-fields-using-single-pass-gpu) — signed distance fields via scan conversion; local: —

## Phase 15 — RTX 3060

Features the Cezanne iGPU doesn't have; needs the NVIDIA GPU working under PRIME.

- [ ] **mesh_shaders** — new API: `VK_EXT_mesh_shader` (GL: `GL_NV_mesh_shader`); new idea: meshlets + culling (vs 18)
- [ ] **ray_query** — new API: `VK_KHR_ray_query`, acceleration structures; new idea: ray-traced shadows/AO (vs 22-23), path tracer

- [ ] **Prerequisite:** get the RTX 3060 working under PRIME (Vulkan doesn't list it yet; `nvidia-smi` fails).

## Anytime / open items

- [ ] Dual-window mode: one process, GL window + VK window, shared camera — true side by side
- [ ] GL consuming **SPIR-V** (`GL_ARB_gl_spirv`, `glSpecializeShader`) — alternative to Phase 8's shared-text shaders
- [ ] Bindless / descriptor indexing vs GL bindless textures
- [ ] PBR (metallic-roughness) on top of Phase 11's IBL; geometry shaders, billboards
- [ ] WebGPU as a third column — after Phase 12, when there is a technique set worth porting

---

## NOTES.md template (per technique)

```markdown
# <NN_name>

## What it shows
One paragraph.

## Read
- [GGn chN Title](online URL) — what to read it for; local CD: `GPU-Gems-n-CD-Content/...` (code/shaders/demo) or —
- (other references: opengl-tutorial, Vulkan samples, papers)

## GL path
Objects created, per-frame calls, implicit state/sync the driver handles.

## VK path
Objects created (and when), per-frame recording, explicit barriers/sync, descriptor layout.

## Differences that matter
- ...

## Gotchas hit
- (bugs found while porting, validation messages and what they meant)

## Numbers
LOC gl vs vk, GPU ms on RADV (gl/vk).
```

---

## Decisions log

| Date | Decision | Why |
|---|---|---|
| 2026-09-29 | New repo, shared core + raw per-API backends, no RHI | Learning goal: see API differences, not hide them |
| 2026-09-29 | Two executables | GLFW window is bound to one client API |
| 2026-09-29 | VK 1.3 dynamic rendering + sync2, no VkRenderPass | Modern idiom; render passes explained once in NOTES |
| 2026-09-29 | GL 4.6 core with DSA | Closest GL idiom to VK's explicit objects |
| 2026-09-29 | fmt instead of std::format | GCC 11.4 on Pop!_OS 22.04 |
| 2026-09-29 | Deps via FetchContent, LunarG SDK for glslc/layers | No prebuilt binaries; apt Vulkan tools too old |
| 2026-09-29 | Raw vkAllocateMemory first, VMA later | Learn memory types before abstracting them |
| 2026-09-29 | glslc via `find_program`, not `COMPONENTS glslc` | Stay on CMake 3.22 (jammy); FindVulkan still caches the path |
| 2026-09-29 | GLFW built X11-only on Linux | Wayland backend needs extra dev packages; session is X11 |
| 2026-09-29 | Projection picks `perspectiveRH_NO/ZO` per call via `ClipDepth`, no `GLM_FORCE_DEPTH_ZERO_TO_ONE` | A global define would give `glm::perspective` different meanings in different TUs of one program |
| 2026-09-29 | Tangents are `vec4` (w = handedness), bitangent rebuilt in the shader | Half the data of T+B; standard (glTF) layout. opengl-tutorial stored B and flipped T |
| 2026-09-29 | `indexMesh` before `computeTangents` (exact-match hash dedupe, then per-vertex tangent accumulation) | O(n) instead of opengl-tutorial's O(n²) epsilon search; same result on our assets (Suzanne 2904→590) |
| 2026-09-29 | Camera modes: arcball + fly (GL's free/orbit modes dropped) | Arcball covers orbit; GL's free mode moved along the unnormalised look vector |
| 2026-09-29 | Technique sources compile into the executables, not a static lib | Static-init registration in a static lib gets dead-stripped by the linker |
| 2026-09-29 | `gl::Technique::render(const Frame&)` (not `render()`) | Mirrors VK's `record(cmd, FrameInfo)`; techniques bind FB 0 + viewport themselves |
| 2026-09-29 | GL techniques set all the state their draws need every frame; app resets GL state on switch | Fixes Glint_gl's state leaks; mirrors VK pipelines owning that state |
| 2026-09-29 | Uniforms at explicit `layout(location)`, set via `glProgramUniform*` | No name lookups; closest GL analogue to VK's fixed binding numbers |
| 2026-09-29 | `--frames N` / `--screenshot FILE` in both apps | Scripted runs + visual checks; groundwork for the parity tool |
| 2026-09-30 | `techniques/common/` for the standard-shading shader + std140 struct (04, 05, 07) | Same shader three times otherwise; the struct is shared by GL and VK |
| 2026-09-30 | Uniform blocks use only `mat4`/`vec4` | std140 vec3/array padding can't make C++ and GLSL disagree |
| 2026-09-30 | Sampler objects everywhere (compare mode on the shadow sampler) | Mirrors `VkSampler`; lets the UI read the shadow map without compare |
| 2026-09-30 | Keep the hand-rolled fmt logger (not spdlog); per-run file `logs/<app>_<date>_<time>.log` by default, with a header (commit + dirty, build, args, config, GPU) and per-technique frame-time summaries | For comparing runs; spdlog's extras (rotation, async, sinks) aren't needed, and swapping it in later only touches `log.cpp` |
| 2026-09-30 | VK 01 uses host-visible vertex/index buffers instead of shader-hardcoded vertices | Same data/params as GL 01; staging is 02's lesson |
| 2026-09-30 | Swapchain `B8G8R8A8_UNORM` (not `_SRGB`) | Matches GL's default framebuffer (no sRGB encode), so GL and VK output compare; gamma is its own technique later |
| 2026-09-30 | Synchronization validation on; only VALIDATION/PERFORMANCE messages count as issues | Sync bugs are the class GL never lets you make; loader (GENERAL) messages describe the environment |
| 2026-09-30 | `vk::Technique::init(Context&, VkFormat swapchainFormat)` | Pipelines need the color attachment format (dynamic rendering) |
| 2026-09-30 | `-Wno-missing-field-initializers` | `VkFooInfo info{VK_STRUCTURE_TYPE_FOO};` zero-initialises the rest by design |
| 2026-09-30 | vk helpers after 02: Buffer (+staging upload), OneTimeCommands, Image, Mesh, GraphicsPipelineDesc; after 03: createTexture/createSampler | Each wraps code written raw in 01–03; techniques 04+ read like their GL twins |
| 2026-09-30 | Dynamic cull mode (`VK_DYNAMIC_STATE_CULL_MODE`) in every pipeline | Core in 1.3; mirrors GL's per-frame `glEnable(GL_CULL_FACE)` without extra pipelines |
| 2026-09-30 | Staging uploads even though RADV/APU has DEVICE_LOCAL+HOST_VISIBLE memory | Portable path; the APU shortcut is documented in 02's NOTES |
| 2026-09-30 | VMA deferred past 03, then moved to Phase 8 | Raw allocation behind vk::Buffer/Image stayed short and readable through 08 |
| 2026-09-30 | vk helpers after 03: descriptors (layout/pool/sets/writes, pipeline layout), `beginRendering`, `transitionDepthForRendering` | Written raw in 02–03 |
| 2026-09-30 | Barriers before shader reads use `VK_ACCESS_2_SHADER_READ_BIT` | Sync validation reports RAW with `SHADER_SAMPLED_READ` only (07) |
| 2026-09-30 | Flipped viewport everywhere, incl. offscreen passes; readers flip v / negate y in the shadow matrix | One convention; the row-order difference is documented where it matters (07, 08) |
| 2026-09-30 | 10 built from Sascha Willems' uber shader (Glint_vk had the shaders, not the code) | Glint_vk's "specialization_constants" sample was a copy of the dynamic UBO sample |
| 2026-09-30 | ImGui calls only in `update()`/`ui()`, never in `render()`/`record()` | VK records after `ImGui::Render()`; drawing there crashed (10) |
| 2026-10-02 | tinygltf **2.9.7** (not 3.x) | v3 is a new C API; 2.9 is the C++ API Sascha Willems' loader (and Glint_vk's `vks/`) uses |
| 2026-10-02 | Large sample models downloaded at configure time (SHA256-pinned) into gitignored `assets/gltf/` | Keeps the repo small; `-DGLINT_DOWNLOAD_ASSETS=OFF` to skip |
| 2026-10-02 | `core/gltf` decodes only images referenced by materials | FlightHelmet's unused ORM maps would cost ~80 MB RAM |
| 2026-10-02 | Frame timer reset after a technique switch | Load time (5.8 s for 11 on VK) was counted as one frame and inflated that technique's mean frame time by ~2 ms |
| 2026-10-02 | Pipeline statistics instead of a draw-call counter | GPU-side, symmetric (GL 4.6 core / VK query), no wrapping of raw draw calls |
| 2026-10-02 | `--renderdoc`: GL re-execs itself with `LD_PRELOAD`; VK writes a fixed layer manifest + `VK_ADD_IMPLICIT_LAYER_PATH` | RenderDoc hooks GL by symbol interposition (needs preload), VK via a layer; the tarball's manifest has a build-machine path |
| 2026-10-02 | `--fixed-dt`, and screenshot runs ignore camera input | Deterministic frames for `tools/parity.py`; a stray scroll had moved the camera in one comparison |
| 2026-10-02 | Phases 8–15: the learning path towards GPU Gems (vault: *Glint - Learning Path* + one note per stage) | Incremental: ≤1 new API concept + ≤1 new idea per technique; Gems chapters only after their prerequisites |
| 2026-10-02 | Every NOTES.md gets a **Read** section (GPU Gems chapter + local CD path) | Read the chapter while reviewing the technique; local CD code at `/mnt/e/tree/graphics/nvidia/GPU-Gems` |
| 2026-09-29 | ImGui backends in their own libs (`imgui_backend_gl/vk`) | Each app links only its renderer backend; our warnings don't apply to third-party code |
