# 06_normal_mapping

## What it shows
Per-pixel normals from a tangent-space normal map, on the cylinder from opengl-tutorial chapter 13, with
diffuse, normal and specular textures. The "normal map" checkbox switches to the flat interpolated normal
for comparison.

## GL path
- The mesh is `loadObj` → `indexMesh` (192 → 66 vertices) → `computeTangents`. Tangents are computed
  *after* indexing so that shared vertices average them.
- Tangents are a `vec4` at location 3. The bitangent is rebuilt in the vertex shader as
  `cross(N, T) * w`, where `w` is the handedness. Glint_gl uploaded a separate bitangent buffer.
- The vertex shader builds TBN in camera space and moves the light and eye directions into tangent space.
  The fragment shader lights with the normal from the map.
- Three textures on units 0–2, with one sampler object bound to all three units.

## VK path
*(Phase 5.)*

## Differences that matter
- **Three textures.** GL: three units. VK: three combined-image-sampler bindings in one descriptor set,
  or one binding with `descriptorCount = 3`.
- **Vertex input.** The first mesh with four attributes. In VK that's four `VkVertexInputAttributeDescription`s
  and, with this non-interleaved layout, four bindings.

## Gotchas hit
- Like opengl-tutorial, the normal map is read with v flipped (`vec2(uv.x, -uv.y)`), because that's how
  this map was authored. Compared side by side with Glint_gl from the same camera, the bumps are lit the
  same way: brick top edges bright, bottom edges dark. So storing the tangent as a `vec4` with handedness
  didn't change the result. Ours is somewhat brighter, from the `V` fix below.
- Glint_gl passed view-projection as `V` here too, which also skewed the tangent-space vectors.

## Numbers
LOC: gl.cpp 78. VK: tbd.
