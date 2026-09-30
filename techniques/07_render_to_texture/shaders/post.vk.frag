#version 460

// Post-process pass: the same effect as post.gl.frag. Two VK differences, marked below.

layout(location = 0) in vec2 vUv;

layout(std140, set = 0, binding = 0) uniform Post {
  vec4 timeWobble;   // x = time, y = wobble strength
  vec4 depthParams;  // x = near, y = far, z = visible range, w = 1: show depth
} u;

layout(set = 0, binding = 1) uniform sampler2D uColor;
layout(set = 0, binding = 2) uniform sampler2D uDepth;

layout(location = 0) out vec4 outColor;

// VK difference 1: the depth buffer stores NDC z directly (already [0,1], perspectiveRH_ZO), so there's no
// "d * 2 - 1" step like in GL; and the ZO projection gives a different inverse.
float linearDepth(float d, float near, float far) {
  return near * far / (far - d * (far - near));
}

void main() {
  float t = u.timeWobble.x;
  float w = u.timeWobble.y;
  vec2 uv = vUv;
  uv.x += w * (0.005 * sin(t + 1024.0 * vUv.x) + 0.01 * sin(vUv.y * 10.0 + t));
  uv.y += w * 0.005 * cos(t + 768.0 * vUv.y);

  // VK difference 2: texel row 0 of the offscreen image is the *top* of the picture (VK framebuffers start
  // at the top-left; the flipped viewport keeps +Y up in NDC but rows are still stored top-down).
  // GL's row 0 is the bottom. Same uv, opposite rows: flip v when sampling.
  uv.y = 1.0 - uv.y;

  if (u.depthParams.w > 0.5) {
    float near = u.depthParams.x, far = u.depthParams.y;
    float dist = linearDepth(texture(uDepth, uv).r, near, far);
    float v = 1.0 - clamp((dist - near) / (u.depthParams.z - near), 0.0, 1.0);
    outColor = vec4(vec3(v), 1.0);
  } else {
    outColor = vec4(texture(uColor, uv).rgb, 1.0);
  }
}
