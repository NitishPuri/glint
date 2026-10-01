#version 460
#include "glint.glsl"

// HDR scene pass, fragment stage: everything in *linear* light, written unclamped to an RGBA16F target.

layout(location = 0) in vec3 vPositionWorld;
layout(location = 1) in vec3 vNormalWorld;
layout(location = 2) in vec2 vUv;

UBO(0, 0) Scene {
  mat4 viewProjection;
  vec4 cameraPosition;
  vec4 lightPositions[4];
  vec4 lightColors[4];
  vec4 settings;  // x = 1: linear workflow
} scene;

PUSH_CONSTANTS Draw {
  mat4 model;
  vec4 emissive;
} draw;

// An *sRGB* texture (GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB): texture() returns linear values — the
// hardware decodes, and filtering happens after decoding (as it should).
SAMPLER(0, 1) sampler2D uAlbedo;

layout(location = 0) out vec4 outColor;

vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
  if (draw.emissive.a > 0.0) {  // light marker: just its (HDR) color
    outColor = vec4(draw.emissive.rgb, 1.0);
    return;
  }
  vec3 albedo = texture(uAlbedo, vUv).rgb;
  // Gamma-space mode: undo the hardware decode, i.e. use the raw texture bytes as if they were linear —
  // which is what techniques 01-11 do with their UNORM textures.
  if (scene.settings.x < 0.5) albedo = linearToSrgb(albedo);

  vec3 n = normalize(vNormalWorld);
  vec3 v = normalize(scene.cameraPosition.xyz - vPositionWorld);
  vec3 color = 0.02 * albedo;  // a little ambient
  for (int i = 0; i < 4; ++i) {
    vec3 toLight = scene.lightPositions[i].xyz - vPositionWorld;
    float d2 = max(dot(toLight, toLight), 1e-4);
    vec3 l = toLight * inversesqrt(d2);
    vec3 radiance = scene.lightColors[i].rgb / d2;  // inverse-square falloff: values far above 1 near lights
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 64.0) * 0.3 * step(0.0, dot(n, l));
    color += (albedo * diffuse + vec3(specular)) * radiance;
  }
  outColor = vec4(color, 1.0);  // HDR: not clamped, not encoded
}
