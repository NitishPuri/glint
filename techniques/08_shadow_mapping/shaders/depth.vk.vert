#version 460

// Shadow pass, as depth.gl.vert: positions into the light's clip space, no fragment stage in the pipeline.
// GL passes the matrix as a uniform at location 0; VK as a push constant.

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
  mat4 lightMvp;
} pc;

void main() {
  gl_Position = pc.lightMvp * vec4(inPosition, 1.0);
}
