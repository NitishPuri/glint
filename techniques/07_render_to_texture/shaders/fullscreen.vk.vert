#version 460

// Full-screen triangle without a vertex buffer, as fullscreen.gl.vert — gl_VertexIndex is VK GLSL's
// gl_VertexID. The pipeline has an empty vertex input state; vkCmdDraw(cmd, 3, 1, 0, 0).

layout(location = 0) out vec2 vUv;

void main() {
  vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  // (0,0), (2,0), (0,2)
  vUv = p;                                                        // 0..1 across the screen, (0,0) bottom-left
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
