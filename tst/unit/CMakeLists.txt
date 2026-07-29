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

list(APPEND unit_tests_SOURCES
  test.cpp)

add_executable(unit_tests "${unit_tests_SOURCES}")
target_link_libraries(unit_tests PRIVATE Rummy::rummy catch2_main)

# Deck2 test suite — separate executable so the Deck baseline is never touched
add_executable(unit_tests2 test2.cpp)
target_link_libraries(unit_tests2 PRIVATE Rummy::rummy catch2_main)
# Provide the fixture directory path to test2.cpp
target_compile_definitions(unit_tests2 PRIVATE
  RUMMY_TEST_INPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inputs"
)

# Strict-mode FullDeck test suite.  Uses a separate binary so fatal() failures
# don't terminate the loose-mode test process.
add_executable(unit_tests3_strict test3_strict.cpp)
target_link_libraries(unit_tests3_strict PRIVATE Rummy::rummy catch2_main)
target_compile_definitions(unit_tests3_strict PRIVATE
  RUMMY_TEST_INPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inputs"
)

# DeckGraph (linked-tree) tests for FullDeck.
add_executable(unit_tests4_graph test4_graph.cpp)
target_link_libraries(unit_tests4_graph PRIVATE Rummy::rummy catch2_main)
target_compile_definitions(unit_tests4_graph PRIVATE
  RUMMY_TEST_INPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inputs"
)

enable_testing()
add_test(NAME run_units COMMAND unit_tests)
add_test(NAME run_units2 COMMAND unit_tests2)
add_test(NAME run_units3_strict COMMAND unit_tests3_strict)
add_test(NAME run_units4_graph COMMAND unit_tests4_graph)
add_custom_target(run_tests 
  COMMAND ${CMAKE_CTEST_COMMAND} -V
  DEPENDS test
  COMMENT "Running unit tests"
)
