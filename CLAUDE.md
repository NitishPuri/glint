# CLAUDE.md — glint

Context for Claude (and future me) working in this repo. Read this first, then `docs/PLAN.md`.

## What this is

A **learning sandbox for OpenGL and Vulkan side by side**. Every rendering technique is implemented
**twice** — once against raw OpenGL 4.6, once against raw Vulkan 1.3 — sharing only the non-GPU parts
(window, input, camera, asset loading, UI, scene parameters). The point is to *see* where the two APIs
differ, not to hide those differences.

It is **not** an engine. There is deliberately **no RHI / backend abstraction**. If you find yourself
writing `IBuffer`, `IDevice`, `CommandList` interfaces that both backends implement — stop. That is the
thing this project exists to avoid.

Success = for each technique, I can open `techniques/<name>/gl.cpp` and `vk.cpp` next to each other,
read `NOTES.md`, and understand every line and every difference.

## Owner / working style

- Owner: Nitish (graphics hobbyist, C++, comfortable with GL basics, learning Vulkan internals).
- AI writes a lot of the code; the owner reads, compares and tweaks. So:
  - **Code is for reading.** Prefer explicit, linear code over clever helpers. A `vk.cpp` that shows
    every `VkXxxCreateInfo` is better than one that hides it behind a builder — *until* that technique has
    been done raw once. Helpers are introduced only after the raw version exists in an earlier technique.
  - Comment the **why** and the **GL↔VK difference**, not the what. E.g.
    `// VK: clip-space Z is [0,1], GL is [-1,1] — hence camera.projection(aspect, ClipDepth::ZeroToOne)`.
  - Every technique ships a `NOTES.md` (see template in `docs/PLAN.md`).
- Work in the phase order of `docs/PLAN.md`. Tick checkboxes there as things land. Don't jump ahead.

## Environment (as of 2026-09-29)

- Pop!_OS 22.04 (Ubuntu jammy base), X11 session, no passwordless sudo — ask the owner to run
  `! sudo apt install ...` when packages are needed.
- Repo lives on an NTFS mount (`/mnt/e/...`): builds are slower than on ext4; file modes show as 755;
  keep `core.filemode=false`. Line endings are LF (`.gitattributes` enforces it).
- Compiler: **GCC 11.4** → **no `std::format`**. Use `{fmt}` everywhere (`fmt::format`, `fmt::print`).
  Don't use other C++20 library features GCC 11 lacks (`std::format`, `<print>`, ranges `to`, `std::expected`).
- GPUs:
  - AMD Radeon (Renoir iGPU): OpenGL 4.6 core (Mesa radeonsi), Vulkan 1.4 (RADV). **Primary target.**
  - NVIDIA driver 580 is installed (hybrid laptop) but not exposed to Vulkan by default; use
    `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` (GL) /
    `__NV_PRIME_RENDER_OFFLOAD=1` (VK) to try it. Nice for comparing drivers, not required.
  - llvmpipe (software Vulkan) is also listed — make sure device selection prefers discrete/integrated.
- Vulkan: loader + headers 1.3.280 installed (`libvulkan-dev`). **Shader compiler and validation layers
  come from the LunarG SDK tarball** extracted into `~/vulkan-sdk` (no sudo; apt's versions are 1.3.204,
  too old and no `glslc`). `source ~/vulkan-sdk/<ver>/setup-env.sh` before configuring.
- Debugging: RenderDoc (captures both GL and VK), `VK_LAYER_KHRONOS_validation`, GL `KHR_debug`.

## Build

```bash
source ~/vulkan-sdk/*/setup-env.sh           # glslc + validation layers
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
./build/debug/glint_gl                        # OpenGL app
./build/debug/glint_vk                        # Vulkan app
```

- CMake ≥ 3.22, Ninja. Dependencies are fetched with `FetchContent` (GLFW 3.4, GLM, ImGui, fmt, stb,
  tinyobjloader, later tinygltf/VMA). **No prebuilt binaries in the repo**, no `ext/glfw-*.WIN64`.
- GL loader: glad2 generated for **GL 4.6 core** + `GL_KHR_debug`, vendored under `external/glad/`
  (generated once, committed).
- Shaders: GLSL `#version 460`. GL loads `.glsl` text at runtime (hot-reloadable). VK shaders are
  compiled to SPIR-V by `glslc` at build time into `build/<cfg>/shaders/`.
- Windows should keep working (MSVC + `find_package(Vulkan)`), but Linux is what's tested.

## Layout (target — see PLAN.md for when each part appears)

```
glint/
  CLAUDE.md, README.md
  docs/
    PLAN.md               step-by-step migration + integration plan (source of truth for progress)
    gl-vs-vk.md           running cheat-sheet of API differences, grown as techniques land
  cmake/                  deps.cmake (FetchContent), shaders.cmake (glslc rules)
  external/glad/          generated GL loader
  src/
    core/                 API-agnostic: window, input, camera, logger, timer, assets (MeshData/ImageData),
                          ui (ImGui frame + common panels), technique registry, params
    gl/                   thin raw-GL helpers (added only after first raw use): shader, buffer, texture, fbo, debug
    vk/                   raw-VK helpers: context, swapchain, frame sync, buffer/image, descriptors,
                          pipeline, upload, debug utils
    app_gl/main.cpp       glint_gl executable
    app_vk/main.cpp       glint_vk executable
  techniques/
    01_triangle/          gl.cpp, vk.cpp, shaders/{*.gl.vert,*.gl.frag,*.vk.vert,*.vk.frag} or shared, NOTES.md, params.h
    02_cube/ ...
  assets/                 meshes, textures (copied from Glint_gl/res and Glint_vk/res)
  tools/                  parity screenshot diff, etc.
```

### Key rules

- `src/core/` must **never** include `glad`, `<vulkan/vulkan.h>`, or any GL/VK type. It produces CPU data
  (`MeshData{positions, normals, uvs, tangents, indices}`, `ImageData{w,h,channels,pixels}`) that each
  backend uploads its own way.
- A technique = shared `params.h` (a plain struct edited by an ImGui panel in `core/ui`) + `gl.cpp` + `vk.cpp`.
  Both apps show the same panel, so the same knobs exist on both sides.
- Each backend has its **own** technique interface (no common base across APIs):
  - `gl::Technique { init(); update(dt, Frame&); render(const Frame&); ui(); ~dtor }`
  - `vk::Technique { init(vk::Context&, VkFormat swapchainFormat); update(dt, Frame&); record(VkCommandBuffer, const vk::FrameInfo&); ui(); ~dtor }`
  - Both also have `setupCamera(Camera&)` (the technique's start/home camera).
  Registration is by name in each app, e.g. `GLINT_REGISTER_GL("08_shadow_mapping", ShadowMappingGL)`.
- GL and VK apps are **separate executables** (a GLFW window created for GL cannot get a Vulkan surface).
  A dual-window side-by-side mode is a later phase.
- Vulkan style: **Vulkan 1.3 core, dynamic rendering (`vkCmdBeginRendering`) + synchronization2**, no
  `VkRenderPass`/`VkFramebuffer` objects (render passes get one explicit NOTES.md entry explaining the
  legacy model). Raw `vkAllocateMemory` first; VMA is introduced deliberately in a later phase.
- Clip-space conventions: VK asks the shared camera for `ClipDepth::ZeroToOne` (`glm::perspectiveRH_ZO`) and
  flips Y via negative viewport height (VK_KHR_maintenance1, core in 1.1), so the same camera code produces the
  same image. No global `GLM_FORCE_DEPTH_ZERO_TO_ONE`: it would change what `glm::perspective` means per
  translation unit. Documented in `docs/gl-vs-vk.md`.
- Validation must be **clean** (zero VK validation errors, zero GL debug errors of severity ≥ medium) before
  a technique is ticked done.

## Source material (read-only references — do not modify)

- `/mnt/e/tree/graphics/Glint_gl` — OpenGL 4.6 project (ported to Linux on 2026-09-29, builds and runs
  on RADV/radeonsi). 8 scenes in `src/Scenes/*.h` (quad, cube, uv cube, basic shading, VBO indexing, normal
  mapping, render-to-texture, shadow mapping), helpers in `src/Graphics/`, shaders in `shaders/`, assets in
  `res/`. Based on opengl-tutorial.org. Uses glad (compat profile 4.6), GLM, ImGui 1.91.9, tinyobjloader, stb.
  **Read `Glint_gl/docs/` before porting anything from it:**
  - `architecture.md`: frame loop, lifecycle, GL state model
  - `modules.md`: per-class reference with a "Port:" target for each
  - `scenes.md`: per-technique passes, uniforms, and VK-port notes
  - `known-issues.md`: bugs *not* to carry over, e.g. the shadow sampler missing compare mode, RTT uniforms
    never set, textures that are never bound
- `/mnt/e/tree/graphics/Glint_vk` — Vulkan project, still Windows-only build. ~10k lines.
  `src/glint_core/renderer/` (vk_context, swapchain, synchronization_manager, command_manager, descriptor,
  pipeline + builder, render_pass, texture, mesh), `src/glint_core/core/` (window, camera w/ arcball, config,
  logger), `src/samples/` (textured quad, cube, rotating, dynamic UBO, specialization constants),
  `src/minimal/` (single-file triangle + cube — great raw references), `src/glint_core/vks/` (Sascha
  Willems' VulkanDevice + glTF loader). Uses render passes + cached command buffers.
- Other folders in `/mnt/e/tree/graphics/` (opengl-tutorial, vulkan, shaders, ...) may be useful references.

## Conventions

- C++20 (GCC 11 subset), `.clang-format` (Google, 2-space, 120 cols). Namespaces `glint`, `glint::gl`, `glint::vk`.
- Files: `snake_case.cpp/.h`. Types `PascalCase`, functions `camelCase`, members `m_camelCase`.
- Errors: `VK_CHECK(expr)` aborts with file/line + `string_VkResult`; GL errors come via `KHR_debug` callback.
- Logging: `glint::log::info/warn/error` on top of fmt. Every run also writes `logs/<app>_<date>_<time>.log`
  (gitignored) with a header (commit, build, args, config, GPU) and per-technique `summary` lines — compare
  those across runs for regressions. `--log FILE` / `--no-log-file` override.
- Commit per technique-backend (`techniques/05_vbo_indexing: vk`), small and reviewable.
