#include <doctest/doctest.h>

#include "core/config.h"
#include "core/registry.h"

using namespace glint;

TEST_CASE("parseArgs") {
  const char* argv[] = {"glint_vk", "08_shadow_mapping", "--size", "800x600", "--no-vsync", "--no-validation"};
  const Config c = parseArgs(6, argv);
  CHECK(c.technique == "08_shadow_mapping");
  CHECK(c.width == 800);
  CHECK(c.height == 600);
  CHECK_FALSE(c.vsync);
  CHECK_FALSE(c.validation);

  CHECK(c.writeLogFile);  // on by default
  const char* noLog[] = {"x", "--no-log-file", "--log", "a.log"};
  const Config n = parseArgs(4, noLog);
  CHECK_FALSE(n.writeLogFile);
  CHECK(n.logFile == "a.log");

  const char* bad1[] = {"x", "--size", "800"};
  CHECK_THROWS_AS(parseArgs(3, bad1), std::invalid_argument);
  const char* bad2[] = {"x", "--bogus"};
  CHECK_THROWS_AS(parseArgs(2, bad2), std::invalid_argument);
  const char* bad3[] = {"x", "--log"};
  CHECK_THROWS_AS(parseArgs(2, bad3), std::invalid_argument);
}

namespace {
struct Thing {
  virtual ~Thing() = default;
  virtual int id() const = 0;
};
template <int N>
struct ThingN : Thing {
  int id() const override { return N; }
};
}  // namespace

TEST_CASE("Registry: sorted names, create by name, duplicates rejected") {
  Registry<Thing> reg;
  reg.add("02_b", [] { return std::make_unique<ThingN<2>>(); });
  reg.add("01_a", [] { return std::make_unique<ThingN<1>>(); });
  CHECK(reg.names() == std::vector<std::string>{"01_a", "02_b"});
  CHECK(reg.create("02_b")->id() == 2);
  CHECK(reg.create("nope") == nullptr);
  CHECK_THROWS_AS(reg.add("01_a", [] { return std::make_unique<ThingN<3>>(); }), std::logic_error);
}
