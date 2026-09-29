# assets

Copied from the two predecessor projects (no file names collided, nothing was deduplicated).

| File | From | Originally |
|---|---|---|
| `cube.obj`, `cube.png` | `Glint_gl/res` | opengl-tutorial.org (tutorials 7–8) |
| `suzanne.obj`, `suzanne.jpg` | `Glint_gl/res` | opengl-tutorial.org (Blender's Suzanne) |
| `cylinder/{cylinder.obj,diffuse,normal,specular.jpg}` | `Glint_gl/res` | opengl-tutorial.org, tutorial 13 (normal mapping) |
| `room/{room_thickwalls.obj,uvmap.jpg}` | `Glint_gl/res` | opengl-tutorial.org, tutorial 16 (shadow mapping) |
| `textures/{box.jpg,grid.png}` | `Glint_gl/res` | Glint_gl |
| `texture.jpg` | `Glint_vk/res` | vulkan-tutorial.com (texture mapping chapter) |
| `viking_room.{obj,png}` | `Glint_vk/res` | vulkan-tutorial.com (model loading chapter); model by nigelgoh, CC BY 4.0 |

The code finds this directory through the `GLINT_ASSET_DIR` compile definition.
