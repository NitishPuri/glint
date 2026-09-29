# Third-party dependencies, fetched at configure time. Nothing prebuilt lives in the repo.
#
# Archives (not git clones) pinned by tag + SHA256: faster on the NTFS mount and reproducible.
# Libraries without a usable CMake project (imgui, stb, tinyobjloader) are only *populated*
# (SOURCE_SUBDIR points at a directory that doesn't exist, so MakeAvailable skips add_subdirectory)
# and wrapped in our own targets below.

include(FetchContent)

# --- GLFW 3.4 -----------------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# X11 only on Linux for now: the Wayland backend needs wayland-scanner/libxkbcommon dev packages.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
  URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01)

# --- GLM 1.0.1 ----------------------------------------------------------------------------------
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)  # header-only; otherwise it compiles every glm .cpp
FetchContent_Declare(glm
  URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
  URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)

# --- fmt 11.2.0 ---------------------------------------------------------------------------------
FetchContent_Declare(fmt
  URL https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.tar.gz
  URL_HASH SHA256=bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af)

# --- Dear ImGui 1.92.9b (docking branch) --------------------------------------------------------
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.tar.gz
  URL_HASH SHA256=90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc
  SOURCE_SUBDIR _no_cmake)

# --- stb (no releases; pinned commit) -----------------------------------------------------------
FetchContent_Declare(stb
  URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
  URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
  SOURCE_SUBDIR _no_cmake)

# --- tinyobjloader v2.0.0rc13 -------------------------------------------------------------------
FetchContent_Declare(tinyobjloader
  URL https://github.com/tinyobjloader/tinyobjloader/archive/refs/tags/v2.0.0rc13.tar.gz
  URL_HASH SHA256=0feb92b838f8ce4aa6eb0ccc32dff30cb64a891e0ec3bde837fca49c78d44334
  SOURCE_SUBDIR _no_cmake)

FetchContent_MakeAvailable(glfw glm fmt imgui stb tinyobjloader)

# ImGui core only. Platform/renderer backends (imgui_impl_glfw + imgui_impl_opengl3 / imgui_impl_vulkan)
# are compiled into each backend library, so each app only pulls in the one it uses.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_features(imgui PUBLIC cxx_std_20)
set(GLINT_IMGUI_BACKEND_DIR ${imgui_SOURCE_DIR}/backends)

# Header-only: the *_IMPLEMENTATION defines go in exactly one .cpp inside core/assets (Phase 2).
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader SYSTEM INTERFACE ${tinyobjloader_SOURCE_DIR})

# Vendored glad2 loader: GL 4.6 core + GL_KHR_debug (see external/glad/README.md).
add_library(glad STATIC ${PROJECT_SOURCE_DIR}/external/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/external/glad/include)
