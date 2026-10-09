// Color helpers shared by the HDR techniques (12 wrote them inline first). Include after #version.
#ifndef GLINT_COLOR_GLSL
#define GLINT_COLOR_GLSL

// The exact sRGB transfer function (piecewise: linear toe + 2.4 power), linear -> encoded.
vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Krzysztof Narkowicz's fit of the ACES filmic curve.
vec3 acesFitted(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// op: 0 clamp (everything above 1 clips to white), 1 Reinhard (never reaches 1, desaturates highlights
// gently), 2 ACES fitted (filmic: toe + shoulder, more contrast).
vec3 tonemap(vec3 hdr, float op) {
  if (op < 0.5) return clamp(hdr, 0.0, 1.0);
  if (op < 1.5) return hdr / (1.0 + hdr);
  return acesFitted(hdr);
}

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }  // Rec. 709 / sRGB primaries

#endif
