# Large sample assets that don't belong in git: downloaded at configure time into assets/gltf/ (gitignored),
# pinned to a commit and checked by SHA256. file(DOWNLOAD ... EXPECTED_HASH) skips files that are already
# there with the right hash, so this costs nothing after the first configure.
#   -DGLINT_DOWNLOAD_ASSETS=OFF to skip (techniques that need them then show an error instead).

option(GLINT_DOWNLOAD_ASSETS "Download large sample assets (glTF models) at configure time" ON)

# KhronosGroup/glTF-Sample-Assets, Models/FlightHelmet/glTF — CC0 (see assets/README.md).
set(GLINT_GLTF_SAMPLES_COMMIT f36bfdabd1031c3cf6689a50570b8cdf3678b49c)
set(GLINT_FLIGHT_HELMET_FILES
  FlightHelmet.gltf c9c16c8b85749f62f38ec6069700e5388c6894d8e566f51a35cc270255402cd3
  FlightHelmet.bin 9e623a27f837e1cc995d1380da5fd3becf4d29586fefba882fc0ba76b49f94bf
  FlightHelmet_Materials_GlassPlasticMat_BaseColor.png 0914544aabe161046f417274051e34ecc85163cfce6698581b3e1e18c96fe4f1
  FlightHelmet_Materials_GlassPlasticMat_Normal.png c297f18f3b4259fc8b411cdc843a4a44b8cef3e3a53b21cec7b53574e5ef6872
  FlightHelmet_Materials_GlassPlasticMat_OcclusionRoughMetal.png d4519704f15395c7138421a5814b478df549a2411bc6fa97dd6d02ba62dc6c54
  FlightHelmet_Materials_LeatherPartsMat_BaseColor.png c9346ac6daa3ad34d0b36c1e7cf311ccbcc298710ea453cdf3b6ad40dc60013f
  FlightHelmet_Materials_LeatherPartsMat_Normal.png 7fb69b26f80c67df41e87d7344bde9d24dda0be77546b0a6acabc4040694872a
  FlightHelmet_Materials_LeatherPartsMat_OcclusionRoughMetal.png b77e60f175e4632cd3d892bc9f0e748f522c9288a469585d5cb90270cd68910e
  FlightHelmet_Materials_LensesMat_BaseColor.png b55f6561f8adabc907360443aab010f3effaf05074fdccea46d8c2d66247cf16
  FlightHelmet_Materials_LensesMat_Normal.png 430775894001933044c89070b1bfd4f9b8489f74780b1ab7079efb0afca95d8d
  FlightHelmet_Materials_LensesMat_OcclusionRoughMetal.png 35d81a71ab184f9b50f63ff8f3e0f90cfb6c99f9b2d9ed9d9887a902c16a458d
  FlightHelmet_Materials_MetalPartsMat_BaseColor.png 8e26add3ad1b2398d58638580bb1fe56f2fbae3572b4a682b6595a5a26fb1b40
  FlightHelmet_Materials_MetalPartsMat_Normal.png a833fcc12718c02bcba97cbc9760d06c4a711ce7f36c23076e7cf6d4393a80e7
  FlightHelmet_Materials_MetalPartsMat_OcclusionRoughMetal.png 17f3e27b51011b8f98751dae25958e2b850fcf1467f1019bec812553e0e5eb1d
  FlightHelmet_Materials_RubberWoodMat_BaseColor.png f751d643d1f220b300705d9ad57ed284a883eb7d3788469199b752f4381dcc37
  FlightHelmet_Materials_RubberWoodMat_Normal.png bb70753c836c59db7e245e5fe2c9db8eee833d29d67e03da348f6adb1e95142a
  FlightHelmet_Materials_RubberWoodMat_OcclusionRoughMetal.png 2727d4638514f75fcbd29777bf03db6bd2f0f1ad926e2976cf2346aa22c0bd19)

function(glint_download_model name subdir files)
  set(base "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/${GLINT_GLTF_SAMPLES_COMMIT}/Models/${name}/${subdir}")
  set(dest "${PROJECT_SOURCE_DIR}/assets/gltf/${name}")
  list(LENGTH files count)
  math(EXPR last "${count} - 1")
  foreach(i RANGE 0 ${last} 2)
    math(EXPR j "${i} + 1")
    list(GET files ${i} file)
    list(GET files ${j} hash)
    if(NOT EXISTS "${dest}/${file}")
      message(STATUS "Downloading ${name}/${file}")
    endif()
    file(DOWNLOAD "${base}/${file}" "${dest}/${file}" EXPECTED_HASH SHA256=${hash} STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      message(WARNING "Download of ${name}/${file} failed: ${status}")
    endif()
  endforeach()
endfunction()

if(GLINT_DOWNLOAD_ASSETS)
  glint_download_model(FlightHelmet glTF "${GLINT_FLIGHT_HELMET_FILES}")
endif()
