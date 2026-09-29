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

Status: Phase 3 done — `glint_gl` runs 01–08. Next: Phase 4, Vulkan foundation.
