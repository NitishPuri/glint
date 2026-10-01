# glint

Learning OpenGL and Vulkan side by side: every technique implemented twice, against raw OpenGL 4.6 and raw
Vulkan 1.3, sharing only API-agnostic code (window, camera, assets, UI).

Successor to [Glint_gl](../Glint_gl) and [Glint_vk](../Glint_vk).

- Plan and progress: [docs/PLAN.md](docs/PLAN.md)
- API differences cheat-sheet: [docs/gl-vs-vk.md](docs/gl-vs-vk.md)
- Context for AI assistants / contributors: [CLAUDE.md](CLAUDE.md)

## Build & run (Linux)

Needs CMake ≥ 3.22, Ninja, X11 dev headers and the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
(for `glslc` and the validation layers) — see Phase 0 in the plan. Other dependencies are fetched by CMake.

```bash
./build.sh            # = source SDK env; cmake -S . -B build/debug -G Ninja; cmake --build build/debug
./run.sh gl           # build/debug/glint_gl
./run.sh vk           # build/debug/glint_vk
```

`./build.sh release` / `CFG=release ./run.sh vk` for an optimized build.

Unit tests (core only): `ctest --test-dir build/debug` or `./build/debug/glint_tests`.

Scripted run with a screenshot (the UI isn't in it): `./run.sh gl 02_cube --screenshot out.png` (60 frames, then quit).

Logs: every run writes `logs/<app>_<date>_<time>.log` (commit, config, GPU, per-technique frame times);
`--no-log-file` to skip it.

Shaders: GL loads `techniques/*/shaders/*.gl.*` at runtime and hot-reloads them when saved.

Comparison tooling:
- `tools/parity.py` renders every technique in both apps (`--fixed-dt`, `--screenshot`) and compares them
  (PSNR; images in `build/debug/parity/`). Exit code 1 if any technique disagrees.
- The stats panel and each run log's `summary` lines show frame, CPU and GPU time (timer queries) and
  pipeline statistics. For cross-API comparisons use `--no-vsync` (GL's CPU time includes driver waits).
- `--renderdoc` loads RenderDoc's in-app API: F12 or the panel button captures a frame into `captures/`,
  and "Open latest in RenderDoc" launches qrenderdoc on it. RenderDoc is found in `~/tools/renderdoc`
  (CMake `GLINT_RENDERDOC_DIR`, or `$RENDERDOC_DIR` at runtime).

Status: Phase 7 done (comparison tooling). Next: Phase 8 (stretch goals).

First configure downloads ~47 MB of glTF sample assets (FlightHelmet) into `assets/gltf/`; `-DGLINT_DOWNLOAD_ASSETS=OFF` skips it.
