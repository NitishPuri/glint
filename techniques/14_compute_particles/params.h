#pragma once

// 14_compute_particles — shared by gl.cpp and vk.cpp. Up to 1M particles that live entirely on the GPU: a
// compute shader integrates them in place in a storage buffer (SSBO) every frame, and the *same* buffer is
// then bound as the vertex buffer of a point draw. The CPU never sees a particle; it only sends a few
// constants per frame. Points are drawn additively into 12's HDR target and tone mapped.

#include <imgui.h>

#include <cmath>
#include <glm/glm.hpp>

namespace glint::particles {

inline const glm::vec3 kCameraHome{0.0f, 3.0f, 11.0f};
inline const glm::vec3 kCameraTarget{0.0f, 2.0f, 0.0f};
inline constexpr uint32_t kMaxParticles = 1u << 20;  // buffer capacity: 1M x 32 bytes = 32 MB
inline constexpr uint32_t kWorkgroupSize = 256;      // must match local_size_x in simulate.comp

// One particle, std430 in the SSBO and also the vertex layout of the draw (stride 32, two vec4 attributes).
struct Particle {
  glm::vec4 position;  // xyz, w = age in seconds (< 0: not born yet, not drawn)
  glm::vec4 velocity;  // xyz, w = lifetime in seconds
};
static_assert(sizeof(Particle) == 32);

struct Params {
  int countLog2 = 20;  // active particles = 2^countLog2 (simulated and drawn)
  bool paused = false;
  float gravity = 3.0f;
  float attractor = 6.0f;  // strength of the two orbiting attractors
  float damping = 0.3f;    // velocity loss per second (air drag)
  float bounce = 0.5f;     // fraction of vertical speed kept when hitting the floor
  float speed = 5.0f;      // launch speed at the emitter
  float brightness = 0.04f;  // HDR radiance per particle (they add up)
  float exposureEv = 0.0f;
  bool reset = true;  // re-seed every particle on the next frame (true: the first frame seeds)

  uint32_t count() const { return 1u << countLog2; }
};

inline void paramsUi(Params& p) {
  ImGui::Text("%u particles", p.count());
  ImGui::SliderInt("count (log2)", &p.countLog2, 10, 20);
  ImGui::Checkbox("paused", &p.paused);
  ImGui::SameLine();
  if (ImGui::Button("reset")) p.reset = true;
  ImGui::SliderFloat("gravity", &p.gravity, 0.0f, 10.0f);
  ImGui::SliderFloat("attractors", &p.attractor, 0.0f, 20.0f);
  ImGui::SliderFloat("damping", &p.damping, 0.0f, 2.0f);
  ImGui::SliderFloat("bounce", &p.bounce, 0.0f, 1.0f);
  ImGui::SliderFloat("launch speed", &p.speed, 0.0f, 10.0f);
  ImGui::SliderFloat("brightness", &p.brightness, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderFloat("exposure", &p.exposureEv, -6.0f, 6.0f, "%+.1f EV");
}

// Compute push constants (GL: the binding-15 block). vec4s only, so std140 and VK agree.
struct SimConstants {
  glm::vec4 time;    // x = dt, y = simulation time, z = 1: reset, w = active count
  glm::vec4 forces;  // x = gravity, y = attractor strength, z = damping, w = bounce
  glm::vec4 emit;    // x = launch speed
};

// Point draw push constants.
struct DrawConstants {
  glm::mat4 viewProjection;
  glm::vec4 params;  // x = brightness
};

// The time step the simulation takes: clamped, so a hitch (window drag, breakpoint) can't explode it.
inline float simDt(float dt, const Params& p) { return p.paused ? 0.0f : std::fmin(dt, 1.0f / 30.0f); }

// Particles the compute pass touches: all of them on a reset (so raising the count later never exposes
// uninitialized memory — neither API zeroes a new buffer), else the active ones.
inline uint32_t simCount(const Params& p) { return p.reset ? kMaxParticles : p.count(); }

inline SimConstants makeSimConstants(const Params& p, float dt, float simTime) {
  return {glm::vec4(dt, simTime, p.reset ? 1.0f : 0.0f, float(simCount(p))),
          glm::vec4(p.gravity, p.attractor, p.damping, p.bounce), glm::vec4(p.speed, 0.0f, 0.0f, 0.0f)};
}

// Workgroups to dispatch: rounded up, the shader skips the excess invocations of the last group.
inline uint32_t groupCount(const Params& p) { return (simCount(p) + kWorkgroupSize - 1) / kWorkgroupSize; }

// Paused: no dispatch at all, unless a reset is pending.
inline bool shouldSimulate(const Params& p) { return !p.paused || p.reset; }

}  // namespace glint::particles
