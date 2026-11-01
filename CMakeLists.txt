cmake_minimum_required(VERSION 3.10)
project(xtexture_compiler LANGUAGES C CXX)
set(TARGET_PROJECT "xtexture_compiler")
set(COMPONENT_REGISTRY "")


# disable strict string conformance. AMD compresator requires it!
if(MSVC)
  add_compile_options(/Zc:strictStrings-)
endif()

include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

add_executable(${TARGET_PROJECT}
  "source/Compiler/main.cpp"
)

source_group("Texture_Compiler" FILES
  "source/Compiler/main.cpp"
)

add_subdirectory("Build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xtexture_compiler")

# The shared xproperty dependency's my_property_ui_null.h isn't present at whatever commit is
# currently fetched - xresource_pipeline.h only needs it when XRESOURCE_PIPELINE_NO_COMPILER is
# undefined, so define it for this compiler-only (no ImGui) target, same fix xfont.plugin's own
# build/dependency/CMakeLists.txt already applies for the identical reason.
target_compile_definitions(${TARGET_PROJECT} PRIVATE XRESOURCE_PIPELINE_NO_COMPILER)

# "dependencies/<lib>/..." includes resolve against this folder first: a library's own nested dependencies/ (xserializer's, from building it
# alone: an old xfile) matched first through its include root and the compiler built against stale headers
target_include_directories(${TARGET_PROJECT} BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Basis Universal (super compression): no SSE4.1 code paths (no per-file flags), no OpenCL, no KTX2 zstd (the texture file's zstd is xserializer's)
target_compile_definitions(${TARGET_PROJECT} PRIVATE BASISU_SUPPORT_SSE=0 BASISU_SUPPORT_OPENCL=0 BASISD_SUPPORT_KTX2_ZSTD=0 XTEXTURE_BASIS_TRANSCODER_LINKED)

# ADD SUPPORT FOR OPEN_MP
find_package(OpenMP REQUIRED)
target_link_libraries(${TARGET_PROJECT} PRIVATE OpenMP::OpenMP_CXX)

# GPU block compression (source/Compiler/xtexture_gpu_encoder.h): only the Vulkan headers - the Vulkan loader is opened at run time, so
# the compiler still runs (on the CPU) where there is no Vulkan driver; without the headers it is built CPU only
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  target_include_directories(${TARGET_PROJECT} PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()
if(UNIX)
  target_link_libraries(${TARGET_PROJECT} PRIVATE ${CMAKE_DL_LIBS})
endif()

ProcessComponents()