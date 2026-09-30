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
Written **raw**, except for the shader loading and memory-type lookup already written out in 01.
- **Geometry.** Positions and indices go to `DEVICE_LOCAL` buffers through a **staging buffer**. The CPU
  writes a host-visible buffer, a one-off command buffer runs `vkCmdCopyBuffer`, then a buffer barrier
  (transfer write → vertex/index read), then `vkQueueWaitIdle`, and the staging buffer is freed.
- **Uniforms.** There's one uniform buffer **per frame in flight**, persistently mapped (host-visible +
  coherent). `record()` `memcpy`s the MVP into the current slot's buffer.
- **Descriptors.** A set layout (binding 0 = uniform buffer, vertex stage), a pool, and 2 sets, one per
  frame slot. Each set points at its slot's buffer, written once with `vkUpdateDescriptorSets`.
  `vkCmdBindDescriptorSets` picks the set for the current slot.
- **Depth.**
  - A `D32_SFLOAT` image with `DEVICE_LOCAL` memory and a view, recreated when the framebuffer size changes
    (after `vkDeviceWaitIdle`).
  - It's transitioned `UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL` every frame. `src` is the previous frame's depth
    writes, because both frames in flight share this one image.
  - It's attached as `pDepthAttachment` with `CLEAR` / `DONT_CARE`.
- **Pipeline.** Like 01's plus depth test/write (`LESS`), back-face culling, and the depth format in
  `VkPipelineRenderingCreateInfo`. The cull mode is **dynamic** (`VK_DYNAMIC_STATE_CULL_MODE`, core in 1.3),
  so the "cull back faces" checkbox doesn't need a second pipeline.
- Helpers extracted after this: `vk::Buffer` + `createDeviceLocalBuffer`, `vk::OneTimeCommands`,
  `vk::Image`, `vk::Mesh`, `vk::GraphicsPipelineDesc` / `createGraphicsPipeline`, `setFlippedViewport`.

## Differences that matter
- **Depth buffer ownership.** GL's default framebuffer already has one. In VK you create the depth
  `VkImage`, allocate and bind its memory, make a view, transition its layout, and attach it in
  `VkRenderingInfo`.
- **Clip-space depth.** GL NDC z is [-1, 1] (`perspectiveRH_NO`). VK is [0, 1] (`perspectiveRH_ZO`). The
  same `Camera` produces both.
- **Uniform delivery.** GL uses a plain uniform. VK needs a uniform buffer + descriptor set (or a push
  constant).

## Memory on this machine (RADV, Renoir APU)
`vulkaninfo` shows 2 heaps: heap 0 is 5.3 GiB of plain system RAM, and heap 1 is 10.6 GiB flagged
`DEVICE_LOCAL`. Heap 1 is *also* system RAM (an APU has no VRAM), just the part the GPU maps fastest. There are
11 memory types, including one that is `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`. So here the CPU could
write "device-local" memory directly, and the staging copy buys nothing. On a discrete GPU, `DEVICE_LOCAL`
is VRAM across PCIe, and the host-visible part of it is small (the 256 MB BAR) or missing without
Resizable BAR. The staging path is the portable one, so that's what we write. VMA exists to make these
per-GPU choices for you. It's deferred: `vk::Buffer`/`vk::Image` keep raw allocation readable, and we're far
from the `maxMemoryAllocationCount` limit.

## Gotchas hit
- The negative-viewport Y flip keeps GL's winding, so `VK_FRONT_FACE_COUNTER_CLOCKWISE` with back-face
  culling shows the same faces as GL (checked side by side: identical image).
- Starting `glint_vk` with a technique name it doesn't know threw halfway through startup, skipping
  ImGui's shutdown. Validation then listed every leaked object. Both apps now check the name before
  creating anything.
- The shader hot reload surfaced that Mesa also reports compile errors through `KHR_debug`. Those are now
  logged at debug level and not counted as API issues (`gl/debug.cpp`).

## Numbers
LOC: gl.cpp 64, vk.cpp 428 (raw: staging, descriptors, depth image and pipeline all spelled out).
