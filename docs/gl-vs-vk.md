# OpenGL vs Vulkan — running cheat-sheet

Grown as techniques land. Each row should link to the technique where it first mattered.

| Topic | OpenGL 4.6 | Vulkan 1.3 | First seen |
|---|---|---|---|
| Window | `GLFW_CLIENT_API = GLFW_OPENGL_API` + version/profile hints; the window owns the context | `GLFW_CLIENT_API = GLFW_NO_API`; you create a `VkSurfaceKHR` for it | 01 |
| Context / device | Implicit, created with the window | Instance → physical device → logical device + queues, all explicit | 01 |
| Presentation | `glfwSwapBuffers` | Swapchain images, acquire/present, semaphores | 01 |
| State | Global state machine (bind-to-edit, or DSA) | Immutable pipeline objects baked up front | 01 |
| Shaders | GLSL text compiled by driver at runtime | SPIR-V compiled offline (`glslc`) | 01 |
| Clip-space depth | [-1, 1] → `ClipDepth::NegOneToOne` (`glm::perspectiveRH_NO`) | [0, 1] → `ClipDepth::ZeroToOne` (`glm::perspectiveRH_ZO`) | 02 |
| Clip-space Y | Up | Down → negative viewport height | 02 |
| Memory | Driver chooses (`glBufferStorage` flags are hints) | You pick memory type/heap, allocate, bind | 02 |
| Uniforms | `glUniform*` / UBO binding points | Descriptor sets + layouts, push constants | 02 |
| Synchronisation | Implicit (driver inserts hazards tracking) | Explicit barriers, fences, semaphores | 07 |
| Command submission | Immediate-looking calls | Record command buffers, submit to queue | 01 |
| Render targets | FBO, attach textures | Dynamic rendering: `VkRenderingInfo` with image views | 07 |
| Debugging | `KHR_debug` callback | Validation layers + debug utils messenger | 01 |
