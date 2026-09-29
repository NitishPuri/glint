#include <doctest/doctest.h>

#include "core/assets.h"

using namespace glint;

namespace {

// Every triangle's geometric normal (from the CCW winding) must agree with its vertex normals.
void checkOutwardWinding(const MeshData& m) {
  for (size_t t = 0; t < m.indices.size(); t += 3) {
    const uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
    const glm::vec3 face = glm::cross(m.positions[i1] - m.positions[i0], m.positions[i2] - m.positions[i0]);
    CHECK(glm::dot(face, m.normals[i0]) > 0.0f);
  }
}

void checkTangentFrame(const MeshData& m) {
  REQUIRE(m.tangents.size() == m.vertexCount());
  for (size_t i = 0; i < m.vertexCount(); ++i) {
    const glm::vec3 t = glm::vec3(m.tangents[i]);
    CHECK(glm::length(t) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(glm::dot(t, glm::normalize(m.normals[i])) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(std::abs(m.tangents[i].w) == 1.0f);
  }
}

}  // namespace

TEST_CASE("makeCube: 24 vertices, 36 indices, outward CCW faces, orthonormal tangents") {
  const MeshData cube = makeCube();
  CHECK(cube.vertexCount() == 24);
  CHECK(cube.indices.size() == 36);
  CHECK(cube.normals.size() == 24);
  CHECK(cube.uvs.size() == 24);
  for (const glm::vec3& p : cube.positions) {
    CHECK(glm::max(glm::abs(p.x), glm::max(glm::abs(p.y), glm::abs(p.z))) == 1.0f);
  }
  checkOutwardWinding(cube);
  checkTangentFrame(cube);
}

TEST_CASE("makeQuad: 4 vertices at z = 0 facing +Z") {
  const MeshData quad = makeQuad();
  CHECK(quad.vertexCount() == 4);
  CHECK(quad.indices.size() == 6);
  for (const glm::vec3& p : quad.positions) CHECK(p.z == 0.0f);
  checkOutwardWinding(quad);
}

TEST_CASE("computeTangents reproduces the cube's analytic tangents") {
  MeshData cube = makeCube();
  const std::vector<glm::vec4> expected = cube.tangents;
  cube.tangents.clear();
  computeTangents(cube);
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(glm::length(glm::vec3(cube.tangents[i]) - glm::vec3(expected[i])) < 1e-5f);
    CHECK(cube.tangents[i].w == expected[i].w);
  }
}

TEST_CASE("indexMesh merges only identical vertices") {
  // Two triangles sharing an edge, as a flat list: 6 vertices, 4 unique.
  MeshData m;
  m.positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}};
  m.normals.assign(6, glm::vec3(0, 0, 1));
  m.uvs = {{0, 0}, {1, 0}, {1, 1}, {1, 1}, {0, 1}, {0, 0}};
  m.indices = {0, 1, 2, 3, 4, 5};
  indexMesh(m);
  CHECK(m.vertexCount() == 4);
  CHECK(m.indices == std::vector<uint32_t>{0, 1, 2, 2, 3, 0});

  SUBCASE("a different uv on the same position is a different vertex") {
    MeshData seam;
    seam.positions = {{0, 0, 0}, {0, 0, 0}};
    seam.uvs = {{0, 0}, {1, 0}};
    seam.indices = {0, 1};
    indexMesh(seam);
    CHECK(seam.vertexCount() == 2);
  }
}

TEST_CASE("indexMesh keeps the cube as is (nothing to merge)") {
  MeshData cube = makeCube();
  const auto before = cube.positions;
  indexMesh(cube);
  CHECK(cube.positions == before);
}

TEST_CASE("loadObj + indexMesh on real assets") {
  MeshData suzanne = loadObj(assetPath("suzanne.obj"));
  CHECK(suzanne.vertexCount() == 2904);  // 968 triangles, flat
  CHECK(suzanne.normals.size() == 2904);
  CHECK(suzanne.uvs.size() == 2904);
  indexMesh(suzanne);
  CHECK(suzanne.vertexCount() == 590);  // same as opengl-tutorial's indexVBO
  CHECK(suzanne.indices.size() == 2904);

  MeshData cylinder = loadObj(assetPath("cylinder/cylinder.obj"));
  indexMesh(cylinder);
  computeTangents(cylinder);
  checkTangentFrame(cylinder);
}

TEST_CASE("loadObj / loadImage throw on missing files") {
  CHECK_THROWS(loadObj(assetPath("does_not_exist.obj")));
  CHECK_THROWS(loadImage(assetPath("does_not_exist.png")));
}

TEST_CASE("loadImage expands to RGBA and flips rows") {
  const ImageData up = loadImage(assetPath("textures/grid.png"), false);
  const ImageData flipped = loadImage(assetPath("textures/grid.png"), true);
  REQUIRE(up.width > 0);
  CHECK(up.channels == 4);
  CHECK(up.sizeBytes() == size_t(up.width) * up.height * 4);
  const size_t row = size_t(up.width) * 4;
  CHECK(std::equal(up.pixels.begin(), up.pixels.begin() + row, flipped.pixels.end() - row));
}
