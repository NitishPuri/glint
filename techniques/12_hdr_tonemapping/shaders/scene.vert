#version 460
#include "glint.glsl"

// HDR scene pass, vertex stage. One source for GL and VK (glint.glsl macros).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

UBO(0, 0) Scene {
  mat4 viewProjection;
  vec4 cameraPosition;
  vec4 lightPositions[4];
  vec4 lightColors[4];
  vec4 settings;
} scene;

// Per draw. VK: push constants. GL: a uniform block at binding point 15, rewritten before each draw.
PUSH_CONSTANTS Draw {
  mat4 model;
  vec4 emissive;
} draw;

layout(location = 0) out vec3 vPositionWorld;
layout(location = 1) out vec3 vNormalWorld;
layout(location = 2) out vec2 vUv;

void main() {
  vec4 world = draw.model * vec4(inPosition, 1.0);
  gl_Position = scene.viewProjection * world;
  vPositionWorld = world.xyz;
  vNormalWorld = mat3(draw.model) * inNormal;  // no non-uniform scale in this scene
  vUv = inUv;
}
