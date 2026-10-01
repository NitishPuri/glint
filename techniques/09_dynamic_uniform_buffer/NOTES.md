# 09_dynamic_uniform_buffer

## What it shows
125 spinning cubes (5×5×5). Each cube's model matrix lives in **one shared uniform buffer**, at
`i * stride`. Before each draw, the shader's per-object block is pointed at that object's slice. This is
the classic "many objects, one buffer" pattern, and the alignment rule is the whole lesson. The random
rotations use a fixed seed, so both apps animate identically. From Glint_vk's `dynamic_uniform_buffer`
sample, after Sascha Willems' `dynamicuniformbuffer`.

## GL path
- `glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT)` gives the alignment, and `stride = alignUp(64, alignment)`.
- Every frame, all 125 matrices are packed into a CPU array at `i * stride` and uploaded with one
  `glNamedBufferSubData`.
- Per draw, `glBindBufferRange(GL_UNIFORM_BUFFER, 1, buffer, i * stride, 64)` points binding point 1 at
  slice *i*, then `glDrawElements`. Binding point 0 holds the per-frame view-projection.

## VK path
- `limits.minUniformBufferOffsetAlignment` gives the alignment. There's one object buffer per frame in flight,
  host-visible, and the matrices are written straight into the mapped memory. No CPU staging copy is needed,
  unlike GL.
- Binding 1 is a `UNIFORM_BUFFER_DYNAMIC` descriptor whose range is one matrix. Per draw,
  `vkCmdBindDescriptorSets(..., 1, &offset)` supplies the offset. There's one dynamic offset per dynamic
  binding in the set, in binding order.
- The shader is the same apart from `set = 0`. A dynamic UBO looks like any other uniform block from GLSL.

## Differences that matter
- **Same idea, different place.** GL rebinds a buffer *range* to a binding point per draw. VK keeps the
  descriptor fixed and passes an *offset* at bind time. Both must respect the device's alignment.
- **Alignment on this machine is 4 bytes in both APIs** (radeonsi and RADV), so the stride is just 64.
  NVIDIA typically reports 256, which makes the stride 256 and the buffer 4× larger. That's exactly why the
  value must be queried.
- **Updating per-frame data.** GL overwrites one buffer (the driver handles in-flight use). VK needs one
  buffer per frame in flight.

## Gotchas hit
- `gl::Buffer::update(ptr, size)` silently resolved to the *template* `update(const T& value, size_t offset)`
  with `T = unsigned char*`. It uploaded the 8-byte pointer at offset 8000, and KHR_debug reported
  `GL_INVALID_VALUE`. The template no longer takes an offset.
- 05's NOTES had guessed 16 bytes for RADV's alignment. The measured value is 4, and that's now corrected.

## Numbers
LOC: gl.cpp 88, vk.cpp 132. At 125 draws the frame time is still vsync-bound (6.9 ms) on both.
