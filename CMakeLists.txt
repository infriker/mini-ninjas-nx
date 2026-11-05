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
if(EXISTS "${MINININJAS_GENERATED_DIR}/minininjas_perf_helpers.h")
  set(manifest "${MINININJAS_GENERATED_DIR}/perf-manifest.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/perf/local_regs.json")
  file(READ "${manifest}" cpu_manifest)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tools/perf/local_regs.json" local_reg_targets)
  string(JSON last_target LENGTH "${local_reg_targets}")
  math(EXPR last_target "${last_target} - 1")
  set(required_hooks sub_82417FF0 sub_82501EF0 sub_824F9340)
  foreach(i RANGE 0 ${last_target})
    string(JSON hook MEMBER "${local_reg_targets}" ${i})
    list(APPEND required_hooks "${hook}")
  endforeach()
  foreach(hook IN LISTS required_hooks)
    string(JSON signature ERROR_VARIABLE missing GET "${cpu_manifest}" nativeOriginals "${hook}")
    if(missing)
      message(FATAL_ERROR "Regenerate the optimized sources for native hook ${hook}. See docs/building.md")
    endif()
  endforeach()
endif()

set(MINININJAS_PGO "off" CACHE STRING "Guest CPU profiling mode (off, generate, use)")
set_property(CACHE MINININJAS_PGO PROPERTY STRINGS off generate use)
if(NOT MINININJAS_PGO MATCHES "^(off|generate|use)$")
  message(FATAL_ERROR "Invalid MINININJAS_PGO: ${MINININJAS_PGO}")
endif()
set(REXGLUE_SWITCH_RUNTIME_PROFILING OFF)
if(MINININJAS_PGO STREQUAL "generate")
  set(REXGLUE_SWITCH_RUNTIME_PROFILING ON)
  # A recorder build must never replace the gameplay SDK's static libraries.
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
include("${MINININJAS_GENERATED_DIR}/sources.cmake")
if(NOT GENERATED_SOURCES)
  message(FATAL_ERROR "Generated source list is empty")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MINININJAS_GENERATED_DIR}/sources.cmake")

# Keep the cold registration table and all weak hook references outside LTO.
set(guest_tables "${MINININJAS_GENERATED_DIR}/minininjas_init.cpp"
                "${MINININJAS_GENERATED_DIR}/minininjas_register.cpp")
list(REMOVE_ITEM GENERATED_SOURCES ${guest_tables})
find_program(MINININJAS_NODE NAMES node nodejs REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set(local_reg_source "${CMAKE_CURRENT_BINARY_DIR}/minininjas_local_regs.cpp")
set(local_reg_dispatch "${CMAKE_CURRENT_BINARY_DIR}/minininjas_local_dispatch.cpp")
set(local_reg_tools "${CMAKE_CURRENT_SOURCE_DIR}/tools/perf")
add_custom_command(OUTPUT "${local_reg_source}" "${local_reg_dispatch}"
  COMMAND "${MINININJAS_NODE}" "${local_reg_tools}/localize_regs.cjs" "${MINININJAS_GENERATED_DIR}"
    "${local_reg_source}" "${local_reg_dispatch}"
  DEPENDS "${local_reg_tools}/localize_regs.cjs" "${local_reg_tools}/local_regs.json" ${GENERATED_SOURCES}
  VERBATIM)
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
  ${guest_tables}
  src/main.cpp
  src/minininjas_coroutines.cpp
  src/minininjas_native.cpp
  src/minininjas_worker_barrier.cpp
  "${local_reg_source}"
  "${local_reg_dispatch}"
  src/switch_heap.cpp
  src/minininjas_gpu.cpp
  src/minininjas_shaders.cpp
  shaders/ds_shader_library.cpp)
set_source_files_properties(src/minininjas_native.cpp PROPERTIES
  COMPILE_OPTIONS "-fno-strict-aliasing;-ffp-contract=off")
target_include_directories(mini-ninjas PRIVATE "${MINININJAS_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_definitions(mini-ninjas PRIVATE MINININJAS_DEKO3D=1 MINININJAS_NATIVE_MSAA=1 MINININJAS_FULL_FRAME=1)
target_link_libraries(mini-ninjas PRIVATE minininjas_recomp rex::runtime)
rexglue_configure_target(mini-ninjas)
rexglue_switch_add_deko3d(mini-ninjas)
option(MINININJAS_LTO "Link-time optimization of translated game functions" ON)
set(MINININJAS_LTO_JOBS 4 CACHE STRING "Parallel GCC LTO workers at the final link")
set(MINININJAS_PROFILE_DIR "" CACHE PATH "Directory containing the recorded GCC .gcda files")
set(nro_name "Mini Ninjas NX")
if(MINININJAS_LTO OR NOT MINININJAS_PGO STREQUAL "off")
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Mini Ninjas LTO and PGO require devkitA64 GCC")
  endif()
endif()
if(MINININJAS_LTO AND NOT MINININJAS_PGO STREQUAL "generate")
  if(NOT MINININJAS_LTO_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "MINININJAS_LTO_JOBS must be a positive integer")
  endif()
  target_compile_options(minininjas_recomp PRIVATE -flto -fno-fat-lto-objects)
  set_property(SOURCE "${local_reg_source}" APPEND PROPERTY COMPILE_OPTIONS -flto -fno-fat-lto-objects)
  target_link_options(mini-ninjas PRIVATE "-flto=${MINININJAS_LTO_JOBS}" -flto-partition=balanced
    -fno-strict-aliasing -ffp-contract=off)
endif()
if(NOT MINININJAS_PGO STREQUAL "off")
  # Stable flat filenames in both build trees. Horizon uses libnx software TLS.
  set(profile_flags "-fprofile-prefix-path=${CMAKE_CURRENT_BINARY_DIR}" -fno-profile-values)
  if(MINININJAS_PGO STREQUAL "generate")
    target_compile_options(minininjas_recomp PRIVATE -fprofile-generate=. ${profile_flags}
      -fprofile-update=atomic -fprofile-info-section)
    target_compile_definitions(mini-ninjas PRIVATE MINININJAS_PGO_RECORD=1)
    target_sources(mini-ninjas PRIVATE src/minininjas_pgo.cpp)
    set(profile_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/perf/gcov-info.ld")
    set_property(TARGET mini-ninjas APPEND PROPERTY LINK_DEPENDS "${profile_script}")
    target_link_libraries(mini-ninjas PRIVATE gcov)
    set(nro_name "Mini Ninjas NX (PGO recorder)")
  else()
    if(NOT IS_DIRECTORY "${MINININJAS_PROFILE_DIR}")
      message(FATAL_ERROR "MINININJAS_PROFILE_DIR must point to a recorded profile")
    endif()
    file(GLOB profile_files CONFIGURE_DEPENDS "${MINININJAS_PROFILE_DIR}/*.gcda")
    if(NOT profile_files)
      message(FATAL_ERROR "No .gcda files in ${MINININJAS_PROFILE_DIR}")
    endif()
    set_property(SOURCE ${GENERATED_SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${profile_files}")
    set(use_flags "-fprofile-use=${MINININJAS_PROFILE_DIR}" ${profile_flags}
      -fprofile-correction -fprofile-partial-training -fprofile-reorder-functions)
    target_compile_options(minininjas_recomp PRIVATE ${use_flags}
      -Wsystem-headers -Werror=missing-profile -Werror=coverage-mismatch)
    target_link_options(mini-ninjas PRIVATE ${use_flags})
  endif()
endif()
rexglue_switch_add_nro(mini-ninjas NAME "${nro_name}" AUTHOR "infriker" VERSION "1.0.8"
  ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg")
