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
*(Phase 4.)*

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

## Numbers
LOC: gl.cpp 161 (about half of it comments). VK: tbd.
