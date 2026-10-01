# 10_specialization_constants

## What it shows
One "uber" fragment shader with three lighting models (Phong, toon, textured). It's built three times with
different constants and drawn side by side in three viewports. The toon desaturation is a constant too,
so moving its slider **rebuilds that variant**, which is the price of "constant". From Sascha Willems'
`specializationconstants`. Glint_vk carried its shaders (`uber.vert/frag`), but its sample code never used
them: its "specialization_constants" sample was a copy of the dynamic UBO one.

## GL path
- One GLSL source (`uber.gl.frag`) with `#if LIGHTING_MODEL == 0 / 1 / else` blocks.
- `gl::Program` takes a `defines` string and inserts it after each stage's `#version` line, plus
  `#line 2` so compiler error line numbers still match the file. Three programs are created with
  `#define LIGHTING_MODEL n` and `#define TOON_DESATURATION x`.
- The preprocessor deletes the other models' code before compilation.
- The slider triggers a full recompile and relink of the toon program.

## VK path
- One SPIR-V module (`uber.vk.frag`) contains all three models behind `switch (LIGHTING_MODEL)`, where
  `layout(constant_id = 0) const int LIGHTING_MODEL = 0;` and `constant_id = 1` is the desaturation.
- Three pipelines are created from the same module, each with a `VkSpecializationInfo`: a small byte blob
  (`{int model; float desat;}`) plus map entries saying which bytes feed which `constant_id`. The driver
  compiles each pipeline with those values as true constants, so dead branches are removed, as with `#if`.
- The slider waits for the GPU to go idle and rebuilds the toon pipeline.
- The three viewports are three negative-height `VkViewport`s with matching scissors.

## Differences that matter
- **Where the variant is made.** In GL it's in the GLSL front end: text substitution, then a full compile
  per variant, at runtime. In VK it's at pipeline creation, from a single precompiled SPIR-V. There's no
  GLSL compiler at runtime, and the SPIR-V is validated once.
- **Types.** Specialization constants are typed (`int`, `float`, `bool`) and have defaults in the shader. `#define`s are just text.
- **What can be specialized.** In VK, constants can also size arrays and set the workgroup size of compute
  shaders (`local_size_x_id`). The GL equivalent is again `#define`, or, in GL 4.6, `glSpecializeShader` on
  SPIR-V (Phase 8's single-source item).
- **Cost of changing one.** It's the same order in both: recompile/link vs pipeline creation. Neither is
  per-frame material. Anything that changes often belongs in a uniform or push constant.

## Gotchas hit
- **VK crashed (segfault) in `record()`.** It drew the viewport labels with ImGui's foreground draw list
  there, but the VK app calls `ImGui::Render()` *before* recording, so the frame's draw lists were already
  closed. In GL the same call happens to work, because `render()` runs before the UI. Labels are now drawn
  in `update()`, and both `technique.h` files say "ImGui calls in `update()`/`ui()` only".

## Numbers
LOC: gl.cpp 90, vk.cpp 162. Images match GL to within 63 edge pixels (1200×500).
