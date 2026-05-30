//========================================================================================
// (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
//========================================================================================

// Strict-mode tests for FullDeck.  The strict mode lowers field assignments
// as `instance.field = value` (no setattr) and requires every instantiated
// class to be supplied via the ctor's class_defs argument.  Writing an
// unknown class is a fatal Rummy error; writing an undeclared field is a
// pips compile-time error.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "full_deck.hpp"
#include <fstream>
#include <sstream>

#define FLOAT_REQUIRE(a, b) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 1e-10))

static std::string fixture(const std::string &name) {
  return std::string(RUMMY_TEST_INPUT_DIR) + "/" + name;
}

// Load a fixture file into a string (used to feed class definitions into the
// FullDeck constructor at "build time").
static std::string slurp(const std::string &path) {
  std::ifstream in(path);
  std::stringstream ss; ss << in.rdbuf();
  return ss.str();
}

TEST_CASE("Strict - basic Gas materials with prepended class defs") {
  const std::string defs = slurp(fixture("strict_classes.par"));
  Rummy::FullDeck d(Rummy::FullDeck::Mode::Strict, defs);
  d.Build(fixture("strict_gas.par"));

  // Schema metadata is the same as loose mode.
  REQUIRE(d.GetClassName("mat1") == "Gas");
  REQUIRE(d.GetClassName("mat2") == "Gas");
  REQUIRE(d.GetClassName("mat1/eos") == "Eos");

  // Scalar fields.
  FLOAT_REQUIRE(d.GetCardValue<double>("mat1", "rho"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat2", "rho"), 2.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat2", "T"), 350.0);

  // Vector field expands into indexed cards on readback.
  REQUIRE(d.IsCardVector("mat1", "T"));
  auto t = d.GetVector<double>("mat1", "T");
  REQUIRE(t.size() == 3);
  FLOAT_REQUIRE(t[0], 300.0);
  FLOAT_REQUIRE(t[1], 100.0);
  FLOAT_REQUIRE(t[2], 200.0);

  // Child instance.
  REQUIRE(d.GetCardValue<std::string>("mat1/eos", "type") == "ideal");
  FLOAT_REQUIRE(d.GetCardValue<double>("mat1/eos", "gamma"), 5.0 / 3.0);
}

TEST_CASE("Strict - default loose mode still uses setattr") {
  // No ctor args = Loose mode = original behavior; user-declared classes
  // optional (auto-emitted as empty stubs otherwise).
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<thing>\n" << "x = 7\n";
  d.Build(ss);
  FLOAT_REQUIRE(d.GetCardValue<double>("thing", "x"), 7.0);
}

// NOTE: Strict-mode failure paths (unknown class, undeclared field) call
// Rummy::fatal which std::abort()s the process, so they cannot be tested
// in-process via Catch2.  They are exercised manually via the `rummy` CLI
// against intentionally-broken inputs.
