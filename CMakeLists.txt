cmake_minimum_required(VERSION 3.25)
project(minininjas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT REXGLUE_PLATFORM_SWITCH OR NOT REXGLUE_SWITCH_DEKO3D)
  message(FATAL_ERROR "Mini Ninjas requires the Switch toolchain and REXGLUE_SWITCH_DEKO3D=ON")
endif()

set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "ReXGlue Switch SDK")
set(MINININJAS_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH "Directory containing sources.cmake and minininjas_init.h")
if(NOT EXISTS "${MINININJAS_GENERATED_DIR}/sources.cmake")
  message(FATAL_ERROR "Generate the game first: ${MINININJAS_GENERATED_DIR}/sources.cmake is missing")
endif()
if(NOT EXISTS "${MINININJAS_GENERATED_DIR}/minininjas_init.h")
  message(FATAL_ERROR "Generated minininjas_init.h missing from ${MINININJAS_GENERATED_DIR}")
endif()

add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
include("${MINININJAS_GENERATED_DIR}/sources.cmake")
if(NOT GENERATED_SOURCES)
  message(FATAL_ERROR "Generated source list is empty")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MINININJAS_GENERATED_DIR}/sources.cmake")

add_library(minininjas_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(minininjas_recomp PRIVATE "${MINININJAS_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(minininjas_recomp PRIVATE rex::runtime)
rexglue_apply_target_settings(minininjas_recomp)
target_precompile_headers(minininjas_recomp PRIVATE "${MINININJAS_GENERATED_DIR}/minininjas_pch.h")
target_compile_options(minininjas_recomp PRIVATE -g0)
# The generated registration function has tens of thousands of calls and runs only once.
set_source_files_properties("${MINININJAS_GENERATED_DIR}/minininjas_register.cpp" PROPERTIES
  COMPILE_OPTIONS "-O1;-g0" SKIP_PRECOMPILE_HEADERS ON)

add_executable(mini-ninjas
  src/main.cpp
  src/minininjas_coroutines.cpp
  src/switch_heap.cpp
  src/minininjas_gpu.cpp
  src/minininjas_shaders.cpp
  shaders/ds_shader_library.cpp)
target_include_directories(mini-ninjas PRIVATE "${MINININJAS_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_definitions(mini-ninjas PRIVATE MINININJAS_DEKO3D=1 MINININJAS_NATIVE_MSAA=1 MINININJAS_FULL_FRAME=1)
target_link_libraries(mini-ninjas PRIVATE minininjas_recomp rex::runtime)
rexglue_configure_target(mini-ninjas)
rexglue_switch_add_deko3d(mini-ninjas)
rexglue_switch_add_nro(mini-ninjas NAME "Mini Ninjas NX" AUTHOR "infriker" VERSION "1.0.3"
  ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg")
