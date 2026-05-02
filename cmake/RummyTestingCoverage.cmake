# ========================================================================================
#  (C) (or copyright) 2026. Triad National Security, LLC. All rights reserved.
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

include_guard(GLOBAL)

include(CTest)

option(RUMMY_ENABLE_COVERAGE "Enable coverage instrumentation for ctest coverage runs" ON)
option(RUMMY_ENABLE_UNIT_TESTS "Enable unit tests" ON)

function(rummy_configure_coverage)
  if(RUMMY_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
      # Coverage reports are most useful with debug symbols and no optimization.
      add_compile_options(-O0 -g --coverage)
      add_link_options(--coverage)

      find_program(RUMMY_GCOV_EXECUTABLE gcov)
      if(RUMMY_GCOV_EXECUTABLE)
        set(CTEST_COVERAGE_COMMAND "${RUMMY_GCOV_EXECUTABLE}" CACHE FILEPATH "Coverage tool used by ctest" FORCE)
      else()
        message(WARNING "RUMMY_ENABLE_COVERAGE is ON, but gcov was not found in PATH. ctest coverage may fail.")
      endif()
    else()
      message(FATAL_ERROR "RUMMY_ENABLE_COVERAGE currently supports only GNU/Clang compilers")
    endif()

    # Limit dashboard coverage to project code and avoid third-party/system noise.
    file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
      "set(CTEST_CUSTOM_COVERAGE_EXCLUDE\n"
      "  \"/usr/.*\"\n"
      "  \".*/Catch2/.*\"\n"
      ")\n"
    )

    add_custom_target(coverage
      COMMAND ${CMAKE_CTEST_COMMAND} -T Test -T Coverage --output-on-failure
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Run tests and generate CTest coverage report"
    )

    find_program(RUMMY_GCOVR_EXECUTABLE gcovr)
    if(RUMMY_GCOVR_EXECUTABLE)
      set(RUMMY_COVERAGE_REPORT_DIR "${CMAKE_BINARY_DIR}/coverage")
      set(RUMMY_COVERAGE_DETAILS_HTML "${RUMMY_COVERAGE_REPORT_DIR}/coverage-details.html")

      add_custom_target(coverage_html
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RUMMY_COVERAGE_REPORT_DIR}"
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ${RUMMY_GCOVR_EXECUTABLE}
                --root "${CMAKE_SOURCE_DIR}"
                --object-directory "${CMAKE_BINARY_DIR}"
                --filter "${CMAKE_SOURCE_DIR}/rummy/"
                --filter "${CMAKE_SOURCE_DIR}/external/pips/"
                --exclude-directories ".*/Catch2/.*"
                --html-details "${RUMMY_COVERAGE_DETAILS_HTML}"
                --no-html-details-syntax-highlighting
                --html-self-contained
                --print-summary
        VERBATIM
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Run tests and generate gcovr detailed HTML coverage report"
      )

      message(STATUS "Coverage HTML report target enabled: coverage_html")
      message(STATUS "Coverage detailed HTML output: ${RUMMY_COVERAGE_DETAILS_HTML}")
    else()
      message(WARNING "gcovr not found in PATH; coverage_html target will not be available")
    endif()
  endif()
endfunction()

function(rummy_add_unit_tests)
  if (RUMMY_ENABLE_UNIT_TESTS)
    # Only configure and add unit-test support when rummy is the top-level project.
    if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
      message("\nConfiguring unit tests...\n")
      find_package(Catch2 QUIET)
      if (NOT Catch2_FOUND)
        # Disable Catch2 installation and docs when building as subproject
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${PROJECT_SOURCE_DIR}/external/Catch2" "${PROJECT_BINARY_DIR}/Catch2")
        list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/external/Catch2/extras")
      endif()
      add_subdirectory("${PROJECT_SOURCE_DIR}/tst" "${PROJECT_BINARY_DIR}/tst")
    else()
      message(STATUS "Rummy unit tests skipped when building as subproject")
    endif()
  endif()
endfunction()