# ========================================================================================
#  (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
#
#  This program was produced under U.S. Government contract 89233218CNA000001 for Los
#  Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
#  for the U.S. Department of Energy/National Nuclear Security Administration. All rights
#  in the program are reserved by Triad National Security, LLC, and the U.S. Department
#  of Energy/National Nuclear Security Administration. The Government is granted for
#  itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
#  license in this material to reproduce, prepare derivative works, distribute copies to
#  the public, perform publicly and display publicly, and to permit others to do so.
# ========================================================================================

# This file was created in part with generative AI

cmake_minimum_required (VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS On)

project ("rummy" LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(RUMMY_ENABLE_ASAN "Enable AddressSanitizer to detect memory errors" OFF)
option(RUMMY_ENABLE_COVERAGE "Enable coverage instrumentation for ctest coverage runs" ON)
option(RUMMY_ENABLE_UNIT_TESTS "Enable unit tests" ON)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
  message(FATAL_ERROR
    "You cannot build in a source directory (or any directory with a CMakeLists.txt file). "
    "Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(RummyTestingCoverage)

if(RUMMY_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fsanitize=undefined)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

if(RUMMY_ENABLE_COVERAGE)
  rummy_configure_coverage()
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  # silence sprintf warnings on mac
  add_compile_options(-Wno-deprecated-declarations)
endif()

# check for pips
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/pips/pips/vm.hpp")
  find_package(Git QUIET)
  if(GIT_FOUND)
    message(STATUS "Rummy: initializing pips git submodule...")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init -- external/pips
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE GIT_SUBMOD_RESULT
    )
  endif()
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/pips/pips/vm.hpp")
    message(FATAL_ERROR
      "pips submodule headers not found at external/pips. "
      "Please run: git submodule update --init")
  endif()
endif()

add_subdirectory("rummy")

if (RUMMY_ENABLE_UNIT_TESTS)
  rummy_add_unit_tests()
endif()
