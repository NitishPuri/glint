# 01_triangle

## What it shows
The minimum to get colored geometry on screen: a GLSL program, a vertex buffer with two interleaved
attributes, an index buffer, and one indexed draw per frame, spun by a per-frame matrix. The "quad"
checkbox draws a second index range from the same buffer. It absorbs Glint_gl's `QuadScene`. Unlike the
original, the transform corrects for the window's aspect ratio, so the quad stays square.

## GL path
Written **raw**: no helpers, every GL call is in `gl.cpp`.
- `init()`:
  - Compile two stages and link them (`glCreateShader`, `glShaderSource`, `glCompileShader`,
    `glCreateProgram`, `glAttachShader`, `glLinkProgram`, then check the info logs).
  - Create the vertex and index buffers with `glCreateBuffers` + `glNamedBufferStorage`. The index buffer
    uses `GL_DYNAMIC_STORAGE_BIT` so both index lists can go in with `glNamedBufferSubData`.
  - Record the VAO with DSA. Binding 0 is the vertex buffer with stride `sizeof(Vertex)`. Locations 0 and 4
    get their format and offset. The element buffer is attached to the VAO.
  - Label every object with `glObjectLabel` so they show up by name in RenderDoc.
- `render()`:
  - Bind framebuffer 0, set the viewport, clear.
  - Set the fixed-function state explicitly (depth test off, culling off).
  - Call `glUseProgram`, set the matrix with `glProgramUniformMatrix4fv` at explicit location 0, bind the VAO,
    then `glDrawElements` with a byte offset into the index buffer.
- The driver does implicitly:
  - moving the buffer data into VRAM
  - validating state at draw time
  - synchronising with the GPU, including freeing objects only once the GPU is done with them
  - presenting, inside `glfwSwapBuffers`

## VK path
Written **raw**: no helpers, every VK object for the technique is in `vk.cpp`.
- `init()`:
  - Two buffers, each with its own `VkDeviceMemory`: `vkCreateBuffer` →
    `vkGetBufferMemoryRequirements` → a loop over `memoryTypes` for `HOST_VISIBLE | HOST_COHERENT` →
    `vkAllocateMemory` → `vkBindBufferMemory` → map, `memcpy`, unmap. On this APU "host visible" is ordinary
    RAM the GPU reads directly. 02 adds staging copies into `DEVICE_LOCAL` memory.
  - A pipeline layout with one push-constant range (the matrix).
  - One graphics pipeline that bakes in everything: both SPIR-V stages (compiled by `glslc` at build
    time), vertex input (binding 0 with stride `sizeof(Vertex)`, attributes at locations 0 and 4),
    triangle-list topology, no culling, no depth, no blending, and the color attachment format
    (`VkPipelineRenderingCreateInfo`, since there's no render pass). Viewport and scissor are *dynamic*,
    so a resize doesn't need a new pipeline.
- `record()`:
  - `vkCmdBeginRendering` on the swapchain view with `loadOp = CLEAR`.
  - Bind the pipeline, set a **negative-height viewport** (the Y flip), then `vkCmdPushConstants`.
  - Bind the vertex and index buffers, then `vkCmdDrawIndexed` with `firstIndex` counted in indices.
  - `vkCmdEndRendering`.
- The app (`app_vk/main.cpp`) owns everything around it: the frame loop, acquire, command buffer,
  layout transitions, submit, present, and swapchain recreation.

## Why does VK need so much more code?
Counting lines including comments: `gl.cpp` is 161 and `vk.cpp` is 281. The bigger difference is outside the
technique. The GL app needs about 200 lines around it plus 110 of debug output. The VK app needs about 400,
plus 750 in `src/vk/` (context, swapchain, frame sync, debug). For "a triangle on screen", that's roughly
**3× the code overall**. The GL driver makes every one of the following decisions for you:

| Decided by the GL driver | Written out in VK |
|---|---|
| Which GPU, which queue | `vkEnumeratePhysicalDevices` + scoring, queue family search, `vkCreateDevice` with opt-in features |
| Back buffer: format, count, vsync, resize | Swapchain: surface format, image count, present mode, recreate on `OUT_OF_DATE` |
| When the CPU may reuse memory the GPU reads | Frames in flight: a fence per frame slot, waited before reuse |
| Ordering of "render, then present" | An acquire semaphore (submit waits on it), a render-done semaphore per swapchain image (present waits on it) |
| Image memory layouts | `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC` barriers with explicit stages and access masks |
| Buffer memory placement | `vkGetBufferMemoryRequirements`, a memory type picked by flags, `vkAllocateMemory`, bind |
| Shader compilation | Offline to SPIR-V; the pipeline also carries all fixed-function state |
| "Draw now" | Record into a command buffer, submit later, and wait for the fence before touching anything again |
| Error checking | Nothing, unless you enable the validation layer (+ synchronization validation) |

None of this is ceremony. Each row is a decision the driver has to guess in GL: which memory, when to
block, how many frames to buffer. VK makes the app decide, which is also why it can decide better.

## Differences that matter
- **Pipeline vs program + state.** A GL program is just the linked shaders. Depth, cull, blend and
  vertex layout are global state you set before each draw, which is why `render()` sets them every time
  and the app resets them when switching techniques. VK bakes all of it into one immutable
  `VkPipeline`.
- **Vertex layout.** DSA's split between a binding (buffer + stride) and an attribute (location + format +
  offset) is exactly VK's `VkVertexInputBindingDescription` / `VkVertexInputAttributeDescription`. The GL
  VAO also stores which buffer is bound. In VK, the buffer is bound at record time.
- **Loose uniforms.** `layout(location = 0) uniform mat4` has no VK equivalent. The matrix becomes a push
  constant there.
- **Index offset units.** `glDrawElements` takes a *byte* offset. `vkCmdDrawIndexed` takes `firstIndex`
  counted in indices.

## Gotchas hit
- `glDeleteShader` right after linking is fine: it only flags them, and the program keeps them alive.
- VK: `vkDestroyShaderModule` right after creating the pipeline is also fine, because the pipeline keeps
  its own compiled copy.
- VK: resetting the frame fence *before* `vkAcquireNextImageKHR` and then bailing out on `OUT_OF_DATE`
  leaves it unsignalled forever, and the next wait deadlocks. The fence is reset only after a successful acquire.
- VK: after a resize, GLFW's size event can arrive a frame *after* present already reported `OUT_OF_DATE`
  and the swapchain was rebuilt. The swapchain was being rebuilt twice until the event was only acted on
  if the size differs.
- The loader warns about a duplicate validation layer (the SDK's plus the distro's old
  `vulkan-validationlayers` package). That's the environment, not our API usage, so GENERAL-type messages
  are logged but not counted as validation issues.
- The shared aspect fix squeezed only x, so in portrait windows the shape *grew*. It now fits the shorter
  side, in both APIs.

## Numbers
LOC: gl.cpp 161, vk.cpp 281 (both about 1/4 comment lines). App + backend: GL ≈ 320, VK ≈ 1150.
Frame time at vsync 144 Hz on RADV/radeonsi: both ≈ 6.9 ms (vsync-bound). GPU timings come in Phase 7.
