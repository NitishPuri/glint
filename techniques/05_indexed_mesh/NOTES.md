# 05_indexed_mesh

## What it shows
The same shading as 04, with an **indexed** mesh: `indexMesh` merges identical vertices, taking Suzanne
from 2904 vertices to 590 (80% fewer) plus a 2904-entry index buffer. The UI shows the numbers. The
mesh is drawn **twice** with different model matrices and materials. Optional alpha blending (unsorted, so
you can see the draw order) replaces Glint_gl's separate `standard_shading_alpha.frag`: alpha is now
just a field in the uniform block.

## GL path
- `gl::Mesh` from the indexed `MeshData`, drawn with `glDrawElements`.
- Two draws, and between them the same UBO is overwritten with `glNamedBufferSubData`.
- Blending: `glEnable(GL_BLEND)` + `glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)`, toggled per frame.

## VK path
- **A dynamic uniform buffer.** There's one buffer per frame slot holding *both* draws' blocks, each at a
  stride of `sizeof(Uniforms)` rounded up to `minUniformBufferOffsetAlignment` (16 on RADV; 64 or 256 on
  other GPUs). The descriptor is `UNIFORM_BUFFER_DYNAMIC` with range = one block. Each draw passes its
  own offset to `vkCmdBindDescriptorSets`. Both blocks are written *before* the command buffer runs, and the
  shader is unchanged.
- **Transparency is a second pipeline** that differs only in `alphaBlend = true`.
- The images match GL to within 1 pixel.

## Differences that matter
- **Per-draw data.** Updating one buffer between draws works in GL because the driver snapshots it for
  each draw. A VK command buffer only records which buffer to read, so both draws would see its final
  contents. Options there: push constants (the model matrix), one buffer region per draw with
  dynamic offsets (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, like Glint_vk's dynamic UBO sample), or
  an instance-data buffer.
- **Blending.** A `glEnable` in GL. In VK it's part of `VkPipelineColorBlendStateCreateInfo`, so the
  transparent mode is a second pipeline (or `VK_EXT_extended_dynamic_state3`).

## Gotchas hit
- The second monkey is at x = +2.5. Glint_gl used +1, which overlapped the first one.

## Numbers
LOC: gl.cpp 94, vk.cpp 152.
