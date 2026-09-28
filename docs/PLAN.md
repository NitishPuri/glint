# glint — Migration & Integration Plan

Goal: merge `Glint_gl` (OpenGL) and `Glint_vk` (Vulkan) into one repo where each rendering technique
exists in both APIs, side by side, sharing only API-agnostic code. See `../CLAUDE.md` for the rules.

Work top to bottom. Each phase ends with a **Done when** check; don't start the next phase until it holds.
Tick boxes as work lands (commit the tick with the work).

Legend: `GL:` = source in `/mnt/e/tree/graphics/Glint_gl`, `VK:` = source in `/mnt/e/tree/graphics/Glint_vk`.

---

## Phase 0 — Machine setup (owner runs these)

- [ ] `sudo apt install cmake ninja-build pkg-config libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`
      (X11 dev headers are needed because GLFW is built from source via FetchContent)
- [ ] LunarG Vulkan SDK (Linux tarball) → `~/vulkan-sdk/`; add `source ~/vulkan-sdk/*/setup-env.sh` to `~/.bashrc`
- [ ] Verify: `glslc --version`, `vulkaninfo --summary` shows RADV, `vkconfig` launches
- [ ] RenderDoc installed (tarball from renderdoc.org → `~/tools/renderdoc`)

**Done when:** the four checks above pass.

---

## Phase 1 — Skeleton & build system

- [ ] `CMakeLists.txt` (root) + `cmake/deps.cmake` with FetchContent:
      glfw 3.4, glm 1.0.1, imgui (docking branch, pinned tag), fmt 11.x, stb, tinyobjloader
- [ ] `find_package(Vulkan REQUIRED COMPONENTS glslc)`; `find_package(OpenGL REQUIRED)`
- [ ] Generate glad2 for **GL 4.6 core** + `GL_KHR_debug` → `external/glad/` (commit generated files)
- [ ] `cmake/shaders.cmake`: function `glint_compile_shaders(target DIR)` → glslc `*.vk.{vert,frag,comp}` → `build/<cfg>/shaders/<technique>/*.spv`, with `-g` in Debug
- [ ] Targets: `glint_core` (static lib), `glint_gl_backend`, `glint_vk_backend`, `glint_gl` (exe), `glint_vk` (exe)
- [ ] Compile definitions: `GLINT_ASSET_DIR`, `GLINT_SHADER_DIR` (source dir for GL, build dir for SPIR-V)
- [ ] Warnings: `-Wall -Wextra -Wpedantic`; `_DEBUG` on Debug for non-MSVC; export `compile_commands.json`
- [ ] Copy assets: `GL:res/*` + `VK:res/*` → `assets/` (dedupe; note origin in `assets/README.md`)
- [ ] `README.md`, `build.sh`, `run.sh <gl|vk> [technique]`

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

- [ ] Each piece above ported and building
- [ ] Unit tests (doctest, FetchContent) for `assets` (cube vertex count, tangent orthogonality, index dedupe) and `camera` (projection clip-depth variants)

**Done when:** `glint_core` builds with zero GL/VK includes (`grep -r "vulkan\|glad" src/core` is empty) and tests pass.

---

## Phase 3 — GL backend + GL app

- [ ] `app_gl/main.cpp`: window(OpenGL) → glad load → KHR_debug callback (from GL:`Core/Sysinfo.cpp`) → ImGui GLFW+OpenGL3 backends → registry → loop
- [ ] Port **01_triangle** raw in `techniques/01_triangle/gl.cpp` (no helpers yet)
- [ ] Introduce `gl/` helpers by extracting from GL:`Graphics/` — `Shader` (with hot reload), `Buffer`, `VertexArray`, `Texture`, `Framebuffer`. Switch to **DSA** (`glCreateBuffers`, `glNamedBufferStorage`, `glTextureStorage2D`…) — GL 4.5+ DSA is the GL idiom closest to Vulkan's explicit objects; note it in `gl-vs-vk.md`
- [ ] Port GL scenes as techniques (shaders move to `techniques/<n>/shaders/*.gl.*`):
  - [ ] 02_cube ← `Scenes/CubeScene.h`
  - [ ] 03_textured_cube ← `Scenes/UVCubeScene.h`
  - [ ] 04_basic_shading ← `Scenes/BasicShading.h`
  - [ ] 05_indexed_mesh ← `Scenes/VBOIndexing.h`
  - [ ] 06_normal_mapping ← `Scenes/NormalMapping.h`
  - [ ] 07_render_to_texture ← `Scenes/RenderToTexture.h`
  - [ ] 08_shadow_mapping ← `Scenes/ShadowMapping.h`
  (`QuadScene` becomes part of 01/07.)
- [ ] Each technique gets `params.h` + shared ImGui panel

**Done when:** `glint_gl` runs all 8 techniques on RADV with no KHR_debug errors, matching the old Glint_gl visually.

---

## Phase 4 — Vulkan foundation + VK app

Port from Glint_vk, but modernise. Read VK:`src/minimal/triangle/main.cpp` first — that's the raw reference.

- [ ] `vk/context` ← VK:`renderer/vk_context.*`: instance (1.3), debug utils messenger, surface, physical device pick (prefer discrete > integrated, **skip llvmpipe/CPU**), queues, enable `dynamicRendering` + `synchronization2` features
- [ ] `vk/swapchain` ← VK:`renderer/swapchain.*`: recreate on resize/`VK_ERROR_OUT_OF_DATE_KHR`
- [ ] `vk/frame` ← VK:`synchronization_manager.*` + `command_manager.*`: **frames in flight = 2**, per-frame command pool/buffer + fence + acquire semaphore; **present semaphore per swapchain image**. Record command buffers every frame (drop Glint_vk's cached command buffers — ImGui needs per-frame recording anyway, see VK commit 2be473a)
- [ ] Transitions via `vkCmdPipelineBarrier2`; small `transitionImage()` helper
- [ ] `vk/debug`: object names + `vkCmdBeginDebugUtilsLabelEXT` per technique pass (shows up in RenderDoc)
- [ ] ImGui Vulkan backend with dynamic rendering (`UseDynamicRendering = true`)
- [ ] **01_triangle** raw in `techniques/01_triangle/vk.cpp`: hardcoded vertices in shader (`gl_VertexIndex`), pipeline with `VkPipelineRenderingCreateInfo`
- [ ] `NOTES.md` for 01 written — the "why does VK need 10× the code" entry

**Done when:** `glint_vk` shows triangle + ImGui, resizes without errors, **zero validation messages**, clean shutdown (no leaked objects reported by validation).

---

## Phase 5 — VK techniques (match the GL set)

Same order as Phase 3. For each: write `vk.cpp` raw-ish, reuse `vk/` helpers only for things already done raw in an earlier technique; then write `NOTES.md`.

- [ ] 02_cube — vertex/index buffers, staging upload, raw `vkAllocateMemory`; UBO + descriptor set; depth image; `GLM_FORCE_DEPTH_ZERO_TO_ONE` + negative viewport. Introduce `vk/buffer` helper after this.
      ← VK:`samples/cube_sample.*`, `minimal/cube/main.cpp`
- [ ] 03_textured_cube — image upload, layout transitions, sampler, combined image sampler descriptors; mipmaps via `vkCmdBlitImage`
      ← VK:`samples/textured_quad.*`, `renderer/texture.*`
- [ ] 04_basic_shading — push constants for model matrix vs GL uniforms
- [ ] 05_indexed_mesh — OBJ via `core/assets`, same mesh data both sides
- [ ] 06_normal_mapping — 3 textures, descriptor set layout with multiple bindings
- [ ] 07_render_to_texture — offscreen color+depth image, two dynamic-rendering passes, barrier between them (vs GL FBO + implicit sync)
- [ ] 08_shadow_mapping — depth-only pass, depth bias (`vkCmdSetDepthBias` vs `glPolygonOffset`), comparison sampler (`sampler2DShadow`), PCF
- [ ] Introduce **VMA** here (after 03 at the latest if raw allocation gets tedious) — `NOTES.md` entry on memory types/heaps on RADV (integrated GPU: device-local + host-visible heap!)

**Done when:** all 8 techniques run on both backends, validation clean, each has `NOTES.md`.

---

## Phase 6 — Bring over Glint_vk-only samples (give them GL twins)

- [ ] 09_dynamic_uniform_buffer ← VK:`samples/dynamic_uniform_buffer.*` | GL twin: `glBindBufferRange` with UBO offset alignment
- [ ] 10_specialization_constants ← VK:`samples/specialization_constants.*` | GL twin: `#define` injection at compile time (+ ARB_gl_spirv specialization in Phase 8)
- [ ] 11_gltf ← VK:`vks/vk_gltf_model.*` (Sascha Willems) — rewrite loader into `core/assets` (tinygltf → `MeshData` + materials), then both backends render it
- [ ] Retire Glint_vk's `vks/VulkanDevice.*` — not needed once `vk/context` exists

**Done when:** everything valuable from both old repos exists in glint. Old repos can be archived.

---

## Phase 7 — Comparison tooling (the payoff)

- [ ] `--screenshot <technique> <out.png>` in both apps (GL `glReadPixels`, VK copy swapchain/offscreen image → host buffer)
- [ ] `tools/parity.py`: runs both apps per technique, diffs images, reports PSNR — catches convention bugs (Y flip, depth range, sRGB)
- [ ] GPU timings: GL `GL_TIME_ELAPSED` queries vs VK timestamp queries (`timestampPeriod`), shown in the same ImGui stats panel
- [ ] CPU frame time + draw-call count side by side
- [ ] RenderDoc in-app capture button (`renderdoc_app.h`) for both apps

**Done when:** `tools/parity.py` passes for all techniques and the stats panel shows GPU ms for both APIs.

---

## Phase 8 — Stretch / new learning

Pick freely; each is a new technique dir with gl + vk + NOTES.
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
