cmake_minimum_required(VERSION 3.22)
project(glint LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)  # uses $VULKAN_SDK when set (source setup-env.sh first)

include(cmake/deps.cmake)
include(cmake/shaders.cmake)
include(cmake/assets.cmake)

# Warnings for our own code only (third-party targets don't link this).
add_library(glint_warnings INTERFACE)
if(MSVC)
  target_compile_options(glint_warnings INTERFACE /W4 /permissive-)
else()
  # -Wno-missing-field-initializers: Vulkan code says `VkFooCreateInfo info{VK_STRUCTURE_TYPE_FOO_CREATE_INFO};`
  # on purpose — C++ guarantees the remaining members are zeroed, which is exactly what VK wants.
  target_compile_options(glint_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
  target_compile_definitions(glint_warnings INTERFACE $<$<CONFIG:Debug>:_DEBUG>)
endif()

# --- core: API-agnostic (never links glad or Vulkan) -------------------------------------------
# Commit hash + dirty flag, refreshed on every build (the script only rewrites the file when it changes).
set(GLINT_GIT_INFO ${CMAKE_BINARY_DIR}/generated/git_info.cpp)
add_custom_target(glint_git_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${GLINT_GIT_INFO}
          -P ${PROJECT_SOURCE_DIR}/cmake/git_info.cmake
  BYPRODUCTS ${GLINT_GIT_INFO}
  COMMENT "git info")

add_library(glint_core STATIC
  ${GLINT_GIT_INFO}
  src/core/assets.cpp
  src/core/camera.cpp
  src/core/config.cpp
  src/core/gltf.cpp
  src/core/input.cpp
  src/core/log.cpp
  src/core/renderdoc.cpp
  src/core/run_log.cpp
  src/core/ui.cpp
  src/core/window.cpp)
target_include_directories(glint_core PUBLIC src)
target_include_directories(glint_core SYSTEM PRIVATE external/renderdoc)
set(GLINT_RENDERDOC_DIR "$ENV{HOME}/tools/renderdoc" CACHE PATH "RenderDoc install dir (lib/librenderdoc.so)")
target_link_libraries(glint_core
  PUBLIC glfw glm::glm fmt::fmt imgui
  PRIVATE stb tinyobjloader tinygltf arcball_camera glint_warnings ${CMAKE_DL_LIBS})
add_dependencies(glint_core glint_git_info)
target_compile_definitions(glint_core
  PUBLIC GLINT_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
  # Per-run log files go here (gitignored).
  PRIVATE GLINT_LOG_DIR="${PROJECT_SOURCE_DIR}/logs"
  # RenderDoc (--renderdoc): where librenderdoc lives (override at runtime with $RENDERDOC_DIR), and where
  # captures go (gitignored).
  PRIVATE GLINT_RENDERDOC_DIR="${GLINT_RENDERDOC_DIR}"
  PRIVATE GLINT_CAPTURE_DIR="${PROJECT_SOURCE_DIR}/captures"
  # Stop <GLFW/glfw3.h> from pulling in the system GL header: core must stay API-free, and the GL
  # backend gets its declarations from glad instead.
  PUBLIC GLFW_INCLUDE_NONE)

# --- OpenGL backend + app ----------------------------------------------------------------------
add_library(imgui_backend_gl STATIC
  ${GLINT_IMGUI_BACKEND_DIR}/imgui_impl_glfw.cpp
  ${GLINT_IMGUI_BACKEND_DIR}/imgui_impl_opengl3.cpp)
target_link_libraries(imgui_backend_gl PUBLIC imgui glfw OpenGL::GL)

add_library(glint_gl_backend STATIC src/gl/debug.cpp src/gl/gpu_queries.cpp src/gl/mesh.cpp src/gl/shader.cpp src/gl/texture.cpp)
target_link_libraries(glint_gl_backend
  PUBLIC glint_core glad OpenGL::GL imgui_backend_gl
  PRIVATE glint_warnings)
# GL loads GLSL text at runtime (hot reload), straight from the source tree.
target_compile_definitions(glint_gl_backend PUBLIC GLINT_SHADER_DIR="${PROJECT_SOURCE_DIR}/techniques")

add_executable(glint_gl src/app_gl/main.cpp)
target_link_libraries(glint_gl PRIVATE glint_gl_backend glint_warnings)

# --- Vulkan backend + app ----------------------------------------------------------------------
add_library(imgui_backend_vk STATIC
  ${GLINT_IMGUI_BACKEND_DIR}/imgui_impl_glfw.cpp
  ${GLINT_IMGUI_BACKEND_DIR}/imgui_impl_vulkan.cpp)
target_link_libraries(imgui_backend_vk PUBLIC imgui glfw Vulkan::Vulkan)

add_library(glint_vk_backend STATIC
  src/vk/buffer.cpp src/vk/context.cpp src/vk/descriptors.cpp src/vk/debug.cpp src/vk/frame.cpp src/vk/gpu_queries.cpp
  src/vk/image.cpp src/vk/mesh.cpp
  src/vk/pipeline.cpp src/vk/shader.cpp src/vk/swapchain.cpp src/vk/texture.cpp src/vk/upload.cpp)
target_link_libraries(glint_vk_backend
  PUBLIC glint_core Vulkan::Vulkan imgui_backend_vk
  PRIVATE glint_warnings)
# VK loads precompiled SPIR-V from the build tree (see cmake/shaders.cmake).
target_compile_definitions(glint_vk_backend PUBLIC GLINT_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")

add_executable(glint_vk src/app_vk/main.cpp)
target_link_libraries(glint_vk PRIVATE glint_vk_backend glint_warnings)
target_compile_definitions(glint_vk PRIVATE GLINT_BUILD_DIR="${CMAKE_BINARY_DIR}")

# --- techniques --------------------------------------------------------------------------------
# Each techniques/<name>/ has gl.cpp and/or vk.cpp. They are compiled straight into the executables, not
# into a library: a static library would let the linker drop their self-registration (core/registry.h).
set(GLINT_TECHNIQUES
  01_triangle
  02_cube
  03_textured_cube
  04_basic_shading
  05_indexed_mesh
  06_normal_mapping
  07_render_to_texture
  08_shadow_mapping
  09_dynamic_uniform_buffer
  10_specialization_constants
  11_gltf
  12_hdr_tonemapping)
# Shaders shared by several techniques (standard shading in 04, 05, 07).
glint_compile_shaders(glint_vk ${PROJECT_SOURCE_DIR}/techniques/common/shaders)
foreach(technique IN LISTS GLINT_TECHNIQUES)
  set(dir ${PROJECT_SOURCE_DIR}/techniques/${technique})
  if(EXISTS ${dir}/gl.cpp)
    target_sources(glint_gl PRIVATE ${dir}/gl.cpp)
  endif()
  if(EXISTS ${dir}/vk.cpp)
    target_sources(glint_vk PRIVATE ${dir}/vk.cpp)
    glint_compile_shaders(glint_vk ${dir}/shaders)
  endif()
endforeach()

# --- unit tests (core only: no window, no GPU) -------------------------------------------------
option(GLINT_BUILD_TESTS "Build unit tests" ON)
if(GLINT_BUILD_TESTS)
  enable_testing()
  add_executable(glint_tests tests/main.cpp tests/test_assets.cpp tests/test_camera.cpp tests/test_config.cpp)
  target_link_libraries(glint_tests PRIVATE glint_core doctest glint_warnings)
  add_test(NAME glint_tests COMMAND glint_tests)
endif()
