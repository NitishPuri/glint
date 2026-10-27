#version 460
#include "glint.glsl"

// One Gray-Scott step, the 2005 GPGPU way (GPU Gems 2 ch31): a full-screen triangle *rendered into* the
// destination image, which is the current color attachment; the source is a plain texture. ch31's vocabulary:
// stream = texture, kernel = fragment program, feedback = render-to-texture, swapped every step.
// GL: two FBOs, glBindFramebuffer per step, nothing else. VK: per step the destination goes to COLOR_ATTACHMENT
// and the source to SHADER_READ_ONLY — two layout transitions that the compute path never needs.

SAMPLER(0, 0) sampler2D uSrc;

// gl_FragCoord is the pixel being written: in GL its origin is the bottom-left, in VK the top-left — and in both
// that is texel (x, y) of the attachment, i.e. the same texel imageStore(p) writes. No flip needed here.
vec2 fetchSim(ivec2 texel) { return texelFetch(uSrc, texel, 0).xy; }

#include "rd.glsl"

layout(location = 0) out vec4 outValue;

void main() { outValue = rdStep(ivec2(gl_FragCoord.xy)); }
