#version 460 core

// A full-screen triangle with no vertex buffer: glDrawArrays(GL_TRIANGLES, 0, 3) and the position comes
// from the vertex index. The triangle covers [-1,3]^2, which clips to exactly the screen.
// VK: same trick with gl_VertexIndex (gl_VertexID is GL-only GLSL).

layout(location = 0) out vec2 vUv;

void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);  // (0,0), (2,0), (0,2)
  vUv = p;                                                  // 0..1 across the screen (bottom-left = 0,0)
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
