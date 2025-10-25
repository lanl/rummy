# ========================================================================================
#  (C) (or copyright) 2025. Triad National Security, LLC. All rights reserved.
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

# Generate library
add_library(rummylib deck.cpp)

# Add alias target for consistency
add_library(Rummy::rummy ALIAS rummylib)

# Include directories
target_include_directories(rummylib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:include>
)

# Compile options
target_compile_features(rummylib PUBLIC cxx_std_17)
if (CMAKE_CXX_COMPILER_ID STREQUAL "XL")
  target_compile_options(rummylib PUBLIC -std=c++1y -qxflag=disable__cplusplusOverride)
endif()

if(NOT RUMMY_STACK_SIZE)
    set(RUMMY_STACK_SIZE 256)
endif()
if (NOT RUMMY_STRING_MAX)
    set(RUMMY_STRING_MAX 64)
endif()
target_compile_definitions(rummylib PUBLIC STACK_MAX=${RUMMY_STACK_SIZE} STRING_MAX=${RUMMY_STRING_MAX})
target_link_libraries(rummylib PUBLIC pipslib)


if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
   message(STATUS "Standalone mode. Adding rummy executable and install targets")
   add_executable(rummy main.cpp)
   target_link_libraries(rummy PUBLIC Rummy::rummy)

  # Installation
  include(GNUInstallDirs)

  install(TARGETS rummylib EXPORT RummyTargets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  # Install headers
  install(DIRECTORY . DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rummy
      FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
  )

  # Export targets
  install(EXPORT RummyTargets
      FILE RummyTargets.cmake
      NAMESPACE Rummy::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Rummy
  )

  # Create config file
  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/cmake/RummyConfigVersion.cmake"
      VERSION "1.0" 
      COMPATIBILITY AnyNewerVersion
  )

  configure_package_config_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RummyConfig.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/RummyConfig.cmake"
      INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Rummy
  )

  install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/RummyConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/RummyConfigVersion.cmake"
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Rummy
  )
else()
  message(STATUS "Rummy built as subproject - no install/export")
endif()