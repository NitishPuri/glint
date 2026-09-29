#pragma once

// 03_textured_cube — shared by gl.cpp and vk.cpp.

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glint::textured_cube {

inline constexpr const char* kTextures[] = {"textures/box.jpg", "textures/grid.png"};

enum class Filter { Nearest, Linear, Trilinear };  // no mips / no mips / mipmapped

struct Params {
  int texture = 0;  // index into kTextures
  Filter filter = Filter::Trilinear;
  float rotationSpeed = 10.0f;  // degrees per second
};

// Returns true if the texture choice changed (the backend reloads the image).
inline bool paramsUi(Params& p) {
  bool changed = false;
  if (ImGui::BeginCombo("texture", kTextures[p.texture])) {
    for (int i = 0; i < int(std::size(kTextures)); ++i) {
      if (ImGui::Selectable(kTextures[i], i == p.texture) && i != p.texture) {
        p.texture = i;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  int filter = int(p.filter);
  ImGui::RadioButton("nearest", &filter, int(Filter::Nearest));
  ImGui::SameLine();
  ImGui::RadioButton("linear", &filter, int(Filter::Linear));
  ImGui::SameLine();
  ImGui::RadioButton("trilinear (mips)", &filter, int(Filter::Trilinear));
  p.filter = Filter(filter);
  ImGui::SliderFloat("rotation", &p.rotationSpeed, -180.0f, 180.0f, "%.0f deg/s");
  return changed;
}

inline glm::mat4 model(float angleDegrees) {
  return glm::rotate(glm::mat4(1.0f), glm::radians(angleDegrees), glm::normalize(glm::vec3(0.5f, 1.0f, 0.0f)));
}

}  // namespace glint::textured_cube
