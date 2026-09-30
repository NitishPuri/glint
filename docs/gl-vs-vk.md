# OpenGL vs Vulkan — running cheat-sheet

Grown as techniques land. Each row should link to the technique where it first mattered.

| Topic | OpenGL 4.6 | Vulkan 1.3 | First seen |
|---|---|---|---|
| Window | `GLFW_CLIENT_API = GLFW_OPENGL_API` + version/profile hints; the window owns the context | `GLFW_CLIENT_API = GLFW_NO_API`; you create a `VkSurfaceKHR` for it | 01 |
| Context / device | Implicit, created with the window | Instance → physical device → logical device + queues, all explicit | 01 |
| Presentation | `glfwSwapBuffers` | Swapchain images, acquire/present, semaphores | 01 |
| Vsync | `glfwSwapInterval(1/0)` | Present mode FIFO / MAILBOX / IMMEDIATE, chosen at swapchain creation | 01 |
| Resize | Nothing to do (driver resizes the default framebuffer) | Recreate the swapchain (`OUT_OF_DATE` / `SUBOPTIMAL`), after the GPU is idle | 01 |
| CPU/GPU overlap | Driver buffers frames, `SwapBuffers` may block | Frames in flight: per-frame command buffer + fence, waited before reuse | 01 |
| Image layouts | Invisible | `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR` via `vkCmdPipelineBarrier2` | 01 |
| Memory for a buffer | Driver's choice | `vkGetBufferMemoryRequirements` → pick a memory type by flags → `vkAllocateMemory` → bind | 01 |
| Destroying objects | Any time; freed once the GPU is done | Only when no queued work uses them (fence / `vkDeviceWaitIdle`) | 01 |
| Y axis | Framebuffer +Y up | +Y down; negative-height viewport restores GL's orientation | 01 |
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
| Render targets | FBO object: `glNamedFramebufferTexture`, completeness check | No object: attachments listed in `VkRenderingInfo` per `vkCmdBeginRendering` | 07 |
| Pass-to-pass hazard | Implicit (driver orders write → sample) | Explicit image barrier + layout transition | 07 |
| Attribute-less draw | `gl_VertexID`, still needs an (empty) VAO bound | `gl_VertexIndex`, empty vertex input state | 07 |
| Stored depth | `d = z_ndc * 0.5 + 0.5` | `d = z_ndc` | 07 |
| Shadow compare | `GL_TEXTURE_COMPARE_MODE` on sampler/texture | `VkSamplerCreateInfo::compareEnable` | 08 |
| Shadow bias matrix | x, y, z: [-1,1] → [0,1] | x, y only (z already [0,1]); mind the Y flip | 08 |
| Depth-only pass | Vertex-only program, `glDrawBuffer(GL_NONE)` | Pipeline without fragment stage / color attachments | 08 |
| Channel swizzle | `GL_TEXTURE_SWIZZLE_RGBA` on the texture | `VkComponentMapping` on an image view | 08 |
| Debugging | `KHR_debug` callback (checks built into the driver, fairly shallow) | Validation layers + debug utils messenger (separate, thorough) | 01 |
| Object names | `glObjectLabel` | `vkSetDebugUtilsObjectNameEXT` | 01 |
| Depth buffer | Comes with the default framebuffer | You create image + memory + view, transition it, attach it | 02 |
| Texture upload | `glTextureStorage2D` + `glTextureSubImage2D` from a CPU pointer | Staging buffer + `vkCmdCopyBufferToImage` + layout transitions | 03 |
| Mipmaps | `glGenerateTextureMipmap` | Loop of `vkCmdBlitImage` + barriers per level | 03 |
| Samplers | Sampler object (mutable) bound per unit, or params on the texture | `VkSampler` (immutable), in a descriptor | 03 |
| Texture binding | Global texture units (`glBindTextureUnit`) | Image view + sampler written into a descriptor set | 03 |
| Uniform blocks | `layout(std140, binding = N)` + `glBindBufferBase` | Same GLSL block as `set/binding`, via descriptor set | 04 |
| Updating in-use buffers | Allowed; driver copies/renames | Race: one buffer per frame in flight | 04 |
| Per-draw data | Update the UBO between draws | Push constants / dynamic UBO offsets / instance buffer | 05 |
| Blending | `glEnable(GL_BLEND)` any time | Pipeline state → a second pipeline | 05 |
