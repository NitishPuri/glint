#pragma once

// Name -> factory map. Instantiated once per backend with that backend's own interface
// (Registry<gl::Technique>, Registry<vk::Technique>); there is no common base class across APIs.
//
// Registration usually happens from a static Registrar in the technique's .cpp (wrapped by the
// GLINT_REGISTER_GL / GLINT_REGISTER_VK macros). Gotcha: if that .cpp is linked from a *static library*,
// the linker drops object files nothing references, and the registration silently never runs. So technique
// sources are compiled straight into the executables.

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace glint {

template <typename T>
class Registry {
 public:
  using Factory = std::function<std::unique_ptr<T>()>;

  static Registry& instance() {
    static Registry registry;  // constructed on first use, so static Registrars in any TU can use it
    return registry;
  }

  void add(std::string name, Factory factory) {
    if (!m_factories.emplace(std::move(name), std::move(factory)).second) {
      throw std::logic_error("Registry: duplicate name");
    }
  }

  bool contains(std::string_view name) const { return m_factories.find(name) != m_factories.end(); }

  // nullptr if the name is unknown.
  std::unique_ptr<T> create(std::string_view name) const {
    auto it = m_factories.find(name);
    return it == m_factories.end() ? nullptr : it->second();
  }

  // Sorted by name, so "01_triangle", "02_cube", ... appear in plan order.
  std::vector<std::string> names() const {
    std::vector<std::string> result;
    for (const auto& entry : m_factories) result.push_back(entry.first);
    return result;
  }

 private:
  std::map<std::string, Factory, std::less<>> m_factories;  // less<> allows lookup by string_view
};

template <typename T>
struct Registrar {
  Registrar(const char* name, typename Registry<T>::Factory factory) {
    Registry<T>::instance().add(name, std::move(factory));
  }
};

}  // namespace glint
