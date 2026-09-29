#version 460 core

// Post-process pass: read the offscreen color (or depth) texture with a wobbly distortion.

layout(location = 0) in vec2 vUv;

layout(std140, binding = 1) uniform Post {
  vec4 timeWobble;   // x = time, y = wobble strength
  vec4 depthParams;  // x = near, y = far, z = visible range, w = 1: show depth
} u;

layout(binding = 0) uniform sampler2D uColor;
layout(binding = 1) uniform sampler2D uDepth;

layout(location = 0) out vec4 outColor;

// Depth buffer value -> distance from the camera.
// GL: the stored depth d in [0,1] came from NDC z in [-1,1] (d = z * 0.5 + 0.5), so undo that first.
// VK: NDC z is already [0,1] and stored as is, so that line goes away (see post.vk.frag).
float linearDepth(float d, float near, float far) {
  float z = d * 2.0 - 1.0;
  return 2.0 * near * far / (far + near - z * (far - near));
}

void main() {
  float t = u.timeWobble.x;
  float w = u.timeWobble.y;
  vec2 uv = vUv;
  uv.x += w * (0.005 * sin(t + 1024.0 * vUv.x) + 0.01 * sin(vUv.y * 10.0 + t));
  uv.y += w * 0.005 * cos(t + 768.0 * vUv.y);

  if (u.depthParams.w > 0.5) {
    float near = u.depthParams.x, far = u.depthParams.y;
    float dist = linearDepth(texture(uDepth, uv).r, near, far);
    // Linear distance: white at the near plane, black at `range` and beyond.
    float v = 1.0 - clamp((dist - near) / (u.depthParams.z - near), 0.0, 1.0);
    outColor = vec4(vec3(v), 1.0);
  } else {
    outColor = vec4(texture(uColor, uv).rgb, 1.0);
  }
}
