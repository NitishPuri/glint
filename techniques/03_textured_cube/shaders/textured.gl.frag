#version 460 core

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// GL: texture *unit* 0; the app binds a texture and a sampler object to that unit.
// VK: `layout(set = 0, binding = 1) uniform sampler2D` — a combined image sampler in a descriptor set.
layout(binding = 0) uniform sampler2D uTexture;

void main() {
  outColor = vec4(texture(uTexture, vUv).rgb, 1.0);
}
