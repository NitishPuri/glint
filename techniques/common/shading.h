#pragma once

// "Standard shading" (opengl-tutorial ch. 8): diffuse texture + Phong-style specular, lit in camera space.
// Shared by 04_basic_shading, 05_indexed_mesh and 07_render_to_texture, in both backends.

#include <imgui.h>

#include <glm/glm.hpp>

namespace glint::shading {

struct Light {
  glm::vec3 position{4.0f, 4.0f, 4.0f};
  glm::vec3 color{1.0f};
  float power = 50.0f;
};

struct Material {
  float ambient = 0.1f;
  float specular = 0.3f;
  float alpha = 1.0f;
};

// The uniform block, byte-for-byte what the shaders declare as
//   layout(std140, binding = 0) uniform Shading { mat4 mvp; mat4 view; mat4 model; vec4 ...; } u;
// std140 rules that bite: a vec3 is aligned to 16 bytes (so vec3 + float can share a slot, but vec3 + vec3
// can't), and array elements are padded to 16 bytes. Using only mat4/vec4 sidesteps both: C++ and GLSL
// layouts can't disagree. Same struct in GL (glNamedBufferSubData) and VK (memcpy into a mapped buffer).
struct Uniforms {
  glm::mat4 mvp;
  glm::mat4 view;   // world -> camera (the original passed view*projection here by mistake)
  glm::mat4 model;  // object -> world
  glm::vec4 lightPosition;    // xyz = world position
  glm::vec4 lightColorPower;  // rgb = color, a = power
  glm::vec4 material;         // x = ambient, y = specular, z = alpha
};
static_assert(sizeof(Uniforms) == 3 * 64 + 3 * 16, "must match the std140 layout of the GLSL block");

inline Uniforms makeUniforms(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
                             const Light& light, const Material& material) {
  return {projection * view * model,
          view,
          model,
          glm::vec4(light.position, 1.0f),
          glm::vec4(light.color, light.power),
          glm::vec4(material.ambient, material.specular, material.alpha, 0.0f)};
}

inline void lightUi(Light& light, float maxPower = 100.0f) {
  ImGui::SliderFloat3("light position", &light.position.x, -10.0f, 10.0f);
  ImGui::ColorEdit3("light color", &light.color.x);
  ImGui::SliderFloat("light power", &light.power, 0.0f, maxPower);
}

inline void materialUi(Material& material) {
  ImGui::SliderFloat("ambient", &material.ambient, 0.0f, 1.0f);
  ImGui::SliderFloat("specular", &material.specular, 0.0f, 5.0f);
}

}  // namespace glint::shading
