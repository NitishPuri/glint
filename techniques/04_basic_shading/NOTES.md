# 04_basic_shading

## What it shows
An OBJ mesh (Suzanne) lit per fragment: ambient, diffuse with 1/d² falloff, and Phong specular, computed in
camera space. The mesh is **not indexed**: 2904 vertices, one per triangle corner, drawn with
`glDrawArrays`. 05 indexes it.

## GL path
- The shader and its uniform block live in `techniques/common/` (shared with 05 and 07).
- **The uniform buffer replaces eight loose uniforms.** `shading::Uniforms` (mat4 and vec4 only) is
  byte-for-byte the GLSL `layout(std140, binding = 0) uniform Shading`. It's rewritten every frame with
  `glNamedBufferSubData` and bound with `glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo)`.
- The texture and sampler use the `gl::Texture` / `gl::Sampler` helpers extracted after 03.

## VK path
*(Phase 5.)*

## Differences that matter
- **std140.** A `vec3` is aligned to 16 bytes, and array elements are padded to 16 bytes. The shared struct
  uses only `mat4`/`vec4`, so the C++ and GLSL layouts can't drift apart. A `static_assert` checks the size.
- **Rewriting a buffer the GPU may be reading.** GL lets you, and the driver copies or renames it for you.
  In VK that's a race: you need one uniform buffer per frame in flight.
- **Binding points vs descriptor sets.** The GL UBO binding point 0 and texture unit 0 become two bindings
  in one VK descriptor set.

## Gotchas hit
- Glint_gl passed the **view-projection** matrix as `V` (known-issues.md), which skewed the camera-space
  lighting. Here `view` really is the view matrix.

## Numbers
LOC: gl.cpp 81. VK: tbd.
