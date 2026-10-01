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
- [ ] `tools/parity.py`: runs both apps per technique, diffs images, reports PSNR — catches convention bugs (Y flip, depth range, sRGB)
- [ ] GPU timings: GL `GL_TIME_ELAPSED` queries vs VK timestamp queries (`timestampPeriod`), shown in the same ImGui stats panel
- [ ] CPU frame time + draw-call count side by side
- [ ] RenderDoc in-app capture button (`renderdoc_app.h`) for both apps

**Done when:** `tools/parity.py` passes for all techniques and the stats panel shows GPU ms for both APIs.

---

## Phase 8 — Stretch / new learning

Pick freely; each is a new technique dir with gl + vk + NOTES.
- [ ] **VMA**: swap `vk::Buffer`/`vk::Image` internals to VMA; NOTES on what it decides (memory type choice,
      sub-allocation, dedicated allocations) vs the raw path. Compare allocation counts in the log.
- [ ] Dual-window mode: one process, GL window + VK window, shared camera — true side by side
- [ ] Single-source shaders: `.glsl` with `#ifdef VULKAN`, and GL consuming **SPIR-V** via `GL_ARB_gl_spirv` (GL 4.6)
- [ ] Compute: particle system (GL compute + SSBO vs VK compute queue + barriers)
- [ ] Instancing, indirect draws (`glMultiDrawElementsIndirect` vs `vkCmdDrawIndexedIndirect`)
- [ ] Deferred shading (MRT), SSAO
- [ ] PBR + IBL (cubemaps, prefiltering) — from Glint_vk README roadmap
- [ ] Skybox, billboards, tessellation, geometry shaders
- [ ] Bindless / descriptor indexing vs GL bindless textures
- [ ] Try on NVIDIA via PRIME offload; note driver differences
- [ ] WebGPU as a third column (see Glint_gl ROADMAP) — only after all above feel boring

---

## NOTES.md template (per technique)

```markdown
# <NN_name>

## What it shows
One paragraph.

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
| 2026-09-29 | ImGui backends in their own libs (`imgui_backend_gl/vk`) | Each app links only its renderer backend; our warnings don't apply to third-party code |
