#version 460

// Additive: each point adds its radiance (blend ONE, ONE into the HDR target), so dense regions get bright
// and the tone mapper compresses them. No depth test or sorting needed: addition is order-independent.

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() { outColor = vec4(vColor, 1.0); }
