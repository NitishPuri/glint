// glint.glsl — included by shaders shared between GL and VK (one source file for both APIs).
//
// glslc predefines VULKAN (= 100) when it compiles for Vulkan; the GL loader (gl::Program) doesn't, so
// `#ifdef VULKAN` picks the per-API syntax. Everything else in a shared shader is plain GLSL 4.60.
//
//   UBO(0, 0) Frame { mat4 viewProjection; } frame;
//   SAMPLER(0, 1) sampler2D uTexture;
//   PUSH_CONSTANTS Draw { mat4 model; } draw;
//   SSBO(0, 2) Particles { Particle p[]; } particles;   // shader storage buffer (std430), from 14
//
// Binding rules that make one number work in both APIs:
//   - VK: (set, binding) of a descriptor set. GL has no sets: `set` is ignored and `binding` is the UBO
//     binding point / texture unit. So binding numbers must be unique per resource type across sets.
//   - GL has no push constants: PUSH_CONSTANTS becomes a uniform block at binding point 15, which the app
//     rewrites before each draw (fine in GL, see 05). Blocks use only mat4/vec4, so std140 (GL) and the
//     std430-style rules of VK push constants give the same layout.
//   - SSBOs have their own binding points in GL (GL_SHADER_STORAGE_BUFFER), separate from UBOs.

#ifdef VULKAN
#define UBO(set_, binding_) layout(std140, set = set_, binding = binding_) uniform
#define SAMPLER(set_, binding_) layout(set = set_, binding = binding_) uniform
#define IMAGE(set_, binding_, format_) layout(set = set_, binding = binding_, format_) uniform
#define PUSH_CONSTANTS layout(push_constant) uniform
#define SSBO(set_, binding_) layout(std430, set = set_, binding = binding_) buffer
#define VERTEX_ID gl_VertexIndex
#else
#define UBO(set_, binding_) layout(std140, binding = binding_) uniform
#define SAMPLER(set_, binding_) layout(binding = binding_) uniform
#define IMAGE(set_, binding_, format_) layout(binding = binding_, format_) uniform
#define PUSH_CONSTANTS layout(std140, binding = 15) uniform
#define SSBO(set_, binding_) layout(std430, binding = binding_) buffer
#define VERTEX_ID gl_VertexID
#endif
