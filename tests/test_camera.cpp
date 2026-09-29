#include <doctest/doctest.h>

#include "core/camera.h"

using namespace glint;

namespace {

// Depth in NDC of a point on the view axis at `distance` in front of the camera.
float ndcDepth(const glm::mat4& proj, float distance) {
  const glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
  return clip.z / clip.w;
}

}  // namespace

TEST_CASE("projection maps near/far to the API's NDC depth range") {
  Camera cam;
  cam.nearZ = 0.1f;
  cam.farZ = 100.0f;

  const glm::mat4 gl = cam.projection(16.0f / 9.0f, ClipDepth::NegOneToOne);
  CHECK(ndcDepth(gl, 0.1f) == doctest::Approx(-1.0f));
  CHECK(ndcDepth(gl, 100.0f) == doctest::Approx(1.0f));

  const glm::mat4 vk = cam.projection(16.0f / 9.0f, ClipDepth::ZeroToOne);
  CHECK(ndcDepth(vk, 0.1f) == doctest::Approx(0.0f));
  CHECK(ndcDepth(vk, 100.0f) == doctest::Approx(1.0f));
}

TEST_CASE("both clip-depth variants agree on x and y (the Y flip is the viewport's job, not the camera's)") {
  Camera cam;
  const glm::mat4 gl = cam.viewProjection(1.5f, ClipDepth::NegOneToOne);
  const glm::mat4 vk = cam.viewProjection(1.5f, ClipDepth::ZeroToOne);
  const glm::vec4 p(0.3f, 0.7f, -0.2f, 1.0f);
  const glm::vec4 a = gl * p, b = vk * p;
  CHECK(a.x / a.w == doctest::Approx(b.x / b.w));
  CHECK(a.y / a.w == doctest::Approx(b.y / b.w));
  CHECK(a.y / a.w > 0.0f);  // world up is +Y in NDC for both
}

TEST_CASE("the target projects to the screen center") {
  Camera cam;
  const glm::vec4 c = cam.viewProjection(1.0f, ClipDepth::ZeroToOne) * glm::vec4(cam.target, 1.0f);
  CHECK(c.x / c.w == doctest::Approx(0.0f));
  CHECK(c.y / c.w == doctest::Approx(0.0f));
}
