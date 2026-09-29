# OpenGL vs Vulkan — running cheat-sheet

Grown as techniques land. Each row should link to the technique where it first mattered.

| Topic | OpenGL 4.6 | Vulkan 1.3 | First seen |
|---|---|---|---|
| Window | `GLFW_CLIENT_API = GLFW_OPENGL_API` + version/profile hints; the window owns the context | `GLFW_CLIENT_API = GLFW_NO_API`; you create a `VkSurfaceKHR` for it | 01 |
| Context / device | Implicit, created with the window | Instance → physical device → logical device + queues, all explicit | 01 |
| Presentation | `glfwSwapBuffers` | Swapchain images, acquire/present, semaphores | 01 |
| State | Global state machine: depth/cull/blend set before each draw, leaks unless reset | Immutable pipeline objects baked up front | 01 |
| Object editing | DSA (4.5): `glNamedBufferStorage(buf, …)`, `glVertexArrayAttribFormat(vao, …)`. Pre-DSA bind-to-edit: `glBindBuffer` + `glBufferData` | Every call names its object: `vkCmdBindVertexBuffers(cmd, …, &buf)` | 01 |
| Vertex layout | VAO: binding (buffer + stride) + attribute (location, format, offset); VAO also remembers the buffer | `VkVertexInputBindingDescription` + `VkVertexInputAttributeDescription` in the pipeline; buffer bound at record time | 01 |
| Per-draw constants | `layout(location = N) uniform` + `glProgramUniform*` | Push constants (`vkCmdPushConstants`), ≤ 128 bytes guaranteed | 01 |
| Index offset | `glDrawElements(…, (void*)byteOffset)` | `vkCmdDrawIndexed(…, firstIndex, …)` in indices | 01 |
| Shaders | GLSL text compiled by driver at runtime | SPIR-V compiled offline (`glslc`) | 01 |
| Clip-space depth | [-1, 1] → `ClipDepth::NegOneToOne` (`glm::perspectiveRH_NO`) | [0, 1] → `ClipDepth::ZeroToOne` (`glm::perspectiveRH_ZO`) | 02 |
| Clip-space Y | Up | Down → negative viewport height | 02 |
| Memory | Driver chooses (`glBufferStorage` flags are hints) | You pick memory type/heap, allocate, bind | 02 |
| Uniforms | `glUniform*` / UBO binding points | Descriptor sets + layouts, push constants | 02 |
| Synchronisation | Implicit (driver inserts hazards tracking) | Explicit barriers, fences, semaphores | 07 |
| Command submission | Immediate-looking calls | Record command buffers, submit to queue | 01 |
| Render targets | FBO, attach textures | Dynamic rendering: `VkRenderingInfo` with image views | 07 |
| Debugging | `KHR_debug` callback (checks built into the driver, fairly shallow) | Validation layers + debug utils messenger (separate, thorough) | 01 |
| Object names | `glObjectLabel` | `vkSetDebugUtilsObjectNameEXT` | 01 |
| Depth buffer | Comes with the default framebuffer | You create image + memory + view, transition it, attach it | 02 |
