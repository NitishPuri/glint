# Vulkan shaders: GLSL -> SPIR-V at build time with glslc.
# (GL shaders are *not* compiled here — the GL app loads the .glsl text at runtime so it can hot-reload.)
#
#   glint_compile_shaders(<target> <dir>)
#
# Compiles every <dir>/*.vk.{vert,frag,comp} to ${CMAKE_BINARY_DIR}/shaders/<technique>/<name>.<stage>.spv,
# where <technique> is the directory above <dir> (techniques/08_shadow_mapping/shaders -> 08_shadow_mapping),
# and makes <target> depend on them. Debug builds keep debug info (-g) so RenderDoc can show GLSL source.

# CMake < 3.24 has no find_package(Vulkan COMPONENTS glslc). FindVulkan still looks for glslc and
# caches Vulkan_GLSLC_EXECUTABLE; this is a no-op if it found one, otherwise it searches the SDK.
find_program(Vulkan_GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin REQUIRED)

function(glint_compile_shaders target dir)
  get_filename_component(dir "${dir}" ABSOLUTE)
  get_filename_component(technique_dir "${dir}" DIRECTORY)
  get_filename_component(technique "${technique_dir}" NAME)
  set(out_dir "${CMAKE_BINARY_DIR}/shaders/${technique}")

  file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.vk.vert" "${dir}/*.vk.frag" "${dir}/*.vk.comp")

  set(outputs "")
  foreach(src IN LISTS sources)
    # foo.vk.vert -> foo.vert.spv (glslc infers the stage from the last extension)
    get_filename_component(file_name "${src}" NAME)
    string(REPLACE ".vk." "." spv_name "${file_name}")
    set(spv "${out_dir}/${spv_name}.spv")
    set(dep "${spv}.d")

    add_custom_command(
      OUTPUT "${spv}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.3 -Werror $<$<CONFIG:Debug>:-g>
              -MD -MF "${dep}" -o "${spv}" "${src}"
      DEPENDS "${src}"
      DEPFILE "${dep}"  # tracks #include'd files
      COMMENT "glslc ${technique}/${file_name}"
      VERBATIM COMMAND_EXPAND_LISTS)
    list(APPEND outputs "${spv}")
  endforeach()

  if(outputs)
    add_custom_target(${target}_shaders_${technique} DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders_${technique})
  endif()
endfunction()
