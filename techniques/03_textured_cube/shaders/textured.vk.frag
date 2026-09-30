#version 460

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// GL: `layout(binding = 0) uniform sampler2D` = texture unit 0, where the app bound texture + sampler.
// VK: binding 1 of descriptor set 0, a *combined image sampler* descriptor (image view + VkSampler).
layout(set = 0, binding = 1) uniform sampler2D uTexture;

void main() {
  outColor = vec4(texture(uTexture, vUv).rgb, 1.0);
}
