# 11_gltf

## What it shows
A real asset: Khronos' **FlightHelmet** (6 meshes, 6 materials, ~95k triangles, 2048² textures). It's
loaded once by `core/gltf` (tinygltf 2.9.7) into plain CPU data (`ModelData`: primitives as `MeshData`,
materials, decoded images, and the node hierarchy flattened into draws). Each backend uploads and draws it.
Shading is base color + normal map + one directional light. PBR (metallic-roughness, IBL) is a Phase 8 item.

The lesson is **binding frequency**: how often each piece of data changes, and where each API wants it.

| Data | Changes | GL | VK |
|---|---|---|---|
| camera, light | per frame | UBO at binding point 0 | descriptor **set 0**, one per frame slot, bound once per frame |
| base color + normal texture | per material | texture units 0–1, rebound when the material changes | descriptor **set 1**, one per material, written once at load |
| model matrix, color factor, alpha cutoff | per draw | 3 uniforms (`glProgramUniform*`) | **push constants**, 96 bytes (VK guarantees 128) |

## GL path
- One `gl::Mesh` per primitive and one `gl::Texture` (with mips) per *used* image, plus 1×1 white and
  flat-normal stand-ins for missing textures.
- Per draw:
  - Only if the material differs from the last one: rebind units 0–1 and switch `GL_CULL_FACE` (off for
    double-sided materials).
  - Then set three uniforms and `glDrawElements`.

## VK path
- One `vk::Mesh` per primitive and one `vk::Image` per used image; all of FlightHelmet's textures upload in
  about 90 ms.
- **Two set layouts**: frame (UBO) and material (two combined image samplers). The pool holds 2 frame sets
  plus one set per material (+1 default). Material sets are written once, because textures never change.
- The pipeline layout is `{set 0 layout, set 1 layout}` plus one push-constant range for the vertex and fragment stages.
- Per frame, set 0 is bound once. Per draw:
  - Only if the material changes: bind set 1 and set the dynamic cull mode.
  - Then `vkCmdPushConstants` and `vkCmdDrawIndexed`. Binding set 1 leaves set 0 bound, because sets are
    independent slots in a compatible pipeline layout.

## Loading (`core/gltf`)
- tinygltf hands every image's *encoded* bytes to our callback. We keep them and decode only images
  referenced by a base-color or normal texture: 10 of FlightHelmet's 15. The 5 occlusion/roughness/metal
  maps would be ~80 MB of RGBA for nothing.
- **No vertical flip**: glTF's uv (0,0) is the image's top-left, i.e. the first row in memory. That's the
  opposite of the OBJ assets, which `loadImage` flips.
- glTF tangents use exactly our `vec4` convention (bitangent = `cross(N, T) * w`), as chosen in Phase 2.
- 16-bit indices are widened to 32 bits, so both backends keep one index type.
- Nodes are walked from the default scene, multiplying TRS/matrix transforms. Normals use the inverse
  transpose in the shader, since node scales may be non-uniform.
- The model is downloaded at configure time (`cmake/assets.cmake`, SHA256-pinned) into the gitignored
  `assets/gltf/`.

## Differences that matter
- **Per-draw data.** GL: three uniform calls, each validated by the driver. VK: one push-constant write
  recorded into the command buffer. That's the cheapest path in VK, but it's limited in size.
- **Per-material data.** GL rebinds textures (cheap calls, but the driver revalidates state). VK switches
  a whole prepared descriptor set with one call. The work moved to load time.
- **Grouping by update frequency** (set 0 frame, set 1 material) is a VK design choice that GL has no word
  for. It's also why set numbers exist at all.

## Gotchas hit
- 🔴 `discard` in the fragment shader produced a validation error: *"SPIR-V Capability
  DemoteToHelperInvocation was declared, but none of the requirements were met"*. glslc targeting Vulkan 1.3
  compiles `discard` to `OpDemoteToHelperInvocation`. That needs the `shaderDemoteToHelperInvocation`
  feature, which is mandatory to *support* in 1.3 but, like every feature, must be *enabled* at device
  creation. It's now enabled in `vk/context.cpp`.
- One early GL/VK comparison showed a different camera distance, and I couldn't reproduce it in four
  later runs (5 pixels apart each time). The likely cause is stray input: the window opens under the mouse
  cursor, and a scroll zooms the arcball.
- The KHR_materials_transmission extension (the glass lenses) is ignored, so the lenses render opaque.

## Numbers
LOC: gl.cpp 107, vk.cpp 170. Load: `loadGltf` 2.2 s (Debug: PNG decode dominates); VK texture upload with
mips 90 ms. 6 draws, 94,722 triangles, vsync-bound on RADV.
