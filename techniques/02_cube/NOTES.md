# 02_cube

## What it shows
3D: model/view/projection, the depth buffer, and back-face culling. The first technique on the `gl/`
helpers (`Program`, `Mesh` = `Buffer` + `VertexArray`), which wrap exactly the calls 01_triangle writes
out. Colors come from the model-space position (`pos * 0.5 + 0.5`). The original used 36 random colors;
position-based colors are deterministic, so GL and VK output can be compared pixel for pixel.

## GL path
- `init()`: `gl::Program` from `cube.gl.{vert,frag}`, and `gl::Mesh(makeCube())`. The mesh has 24 vertices
  (4 per face) and 36 indices, with one buffer per attribute and binding slot = attribute location.
- `update()`: `Program::reloadIfChanged()` (edit the .glsl while the app runs), then advance the angle.
- `render()`:
  - Clear color and depth (`glClearDepth(1)`).
  - Set depth test `GL_LESS`, and `GL_CULL_FACE` with `glFrontFace(GL_CCW)`.
  - Upload `MVP = camera.viewProjection(aspect, ClipDepth::NegOneToOne) * model` to location 0, then draw.
- The depth buffer comes free with the default framebuffer (GLFW asks for 24 bits).

## VK path
*(Phase 5.)*

## Differences that matter
- **Depth buffer ownership.** GL's default framebuffer already has one. In VK you create the depth
  `VkImage`, allocate and bind its memory, make a view, transition its layout, and attach it in
  `VkRenderingInfo`.
- **Clip-space depth.** GL NDC z is [-1, 1] (`perspectiveRH_NO`). VK is [0, 1] (`perspectiveRH_ZO`). The
  same `Camera` produces both.
- **Uniform delivery.** GL uses a plain uniform. VK needs a uniform buffer + descriptor set (or a push
  constant).

## Gotchas hit
- The shader hot reload surfaced that Mesa also reports compile errors through `KHR_debug`. Those are now
  logged at debug level and not counted as API issues (`gl/debug.cpp`).

## Numbers
LOC: gl.cpp 64. VK: tbd.
