//========================================================================================
// (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

// This file was created in part with generative AI

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "full_deck.hpp"
#include "yaml_schema.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>

#define FLOAT_REQUIRE(a, b) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 1e-10))
#define FLOAT_REQUIRE_TOL(a, b, tol) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, tol))

// Resolve a fixture path relative to RUMMY_TEST_INPUT_DIR (defined by CMake)
static std::string fixture(const std::string &name) {
  return std::string(RUMMY_TEST_INPUT_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
// Basic API parity with Deck — ensures Deck2 compiles and behaves correctly
// for the same simple inputs Deck handles.
// ---------------------------------------------------------------------------
TEST_CASE("Deck2 - Basic build and access") {
  GIVEN("A simple inline deck") {
    Rummy::FullDeck d;
    std::stringstream ss;
    ss << "global1 = 42\n"
       << "<suit1>\n"
       << "card1 = global1\n"
       << "card2 = 3.14\n"
       << "<suit2>\n"
       << "card3 = 2 * suit1.card1\n"
       << "card4 = 2.718\n";
    d.Build(ss);

    THEN("Correct suit/card counts") {
      REQUIRE(d.GetDeck().size() == 3); // "/", "suit1", "suit2"
      REQUIRE(d.GetSuit("suit1").size() == 2);
      REQUIRE(d.GetSuit("suit2").size() == 2);
    }

    THEN("Correct values") {
      FLOAT_REQUIRE(d.GetCardValue<double>("suit1", "card1"), 42.0);
      FLOAT_REQUIRE(d.GetCardValue<double>("suit1", "card2"), 3.14);
      FLOAT_REQUIRE(d.GetCardValue<double>("suit2", "card3"), 84.0);
      FLOAT_REQUIRE(d.GetCardValue<double>("suit2", "card4"), 2.718);
    }

    THEN("Correct global value") {
      FLOAT_REQUIRE(d.GetCardValue<double>("/", "global1"), 42.0);
    }
  }
}

TEST_CASE("Deck2 - String values") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "global_str = \"hello\"\n"
     << "<suit1>\n"
     << "card1 = global_str\n"
     << "card2 = \"world\"\n";
  d.Build(ss);
  REQUIRE(d.GetCardValue<std::string>("suit1", "card1") == "hello");
  REQUIRE(d.GetCardValue<std::string>("suit1", "card2") == "world");
}

TEST_CASE("Deck2 - Boolean values") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<s>\n"
     << "t = true\n"
     << "f = false\n";
  d.Build(ss);
  REQUIRE(d.GetCardValue<bool>("s", "t") == true);
  REQUIRE(d.GetCardValue<bool>("s", "f") == false);
}

TEST_CASE("Deck2 - Copy constructor and assignment") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<s>\n"
     << "x = 7\n"
     << "y = \"copy\"\n";
  d.Build(ss);

  Rummy::FullDeck d2(d);
  FLOAT_REQUIRE(d2.GetCardValue<double>("s", "x"), 7.0);
  REQUIRE(d2.GetCardValue<std::string>("s", "y") == "copy");

  Rummy::FullDeck d3;
  d3 = d;
  FLOAT_REQUIRE(d3.GetCardValue<double>("s", "x"), 7.0);
  REQUIRE(d3.GetCardValue<std::string>("s", "y") == "copy");
}

TEST_CASE("Deck2 - Vector assignment a = [...]") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<vsuit>\n"
     << "v = [1, 2, 3]\n";
  d.Build(ss);
  REQUIRE(d.IsCardVector("vsuit", "v"));
  auto vec = d.GetVector<double>("vsuit", "v");
  REQUIRE(vec.size() == 3);
  FLOAT_REQUIRE(vec[0], 1.0);
  FLOAT_REQUIRE(vec[1], 2.0);
  FLOAT_REQUIRE(vec[2], 3.0);
}

TEST_CASE("Deck2 - Slice assignment a[:N] = [...]") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<vs>\n"
     << "a[:3] = [10, 20, 30]\n";
  d.Build(ss);
  REQUIRE(d.IsCardVector("vs", "a"));
  auto vec = d.GetVector<double>("vs", "a");
  REQUIRE(vec.size() == 3);
  FLOAT_REQUIRE(vec[0], 10.0);
  FLOAT_REQUIRE(vec[1], 20.0);
  FLOAT_REQUIRE(vec[2], 30.0);
}

TEST_CASE("Deck2 - Nested suits") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<parent>\n"
     << "p = 1\n"
     << "<parent/child>\n"
     << "c = 2\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("parent"));
  REQUIRE(d.DoesSuitExist("parent/child"));
  FLOAT_REQUIRE(d.GetCardValue<double>("parent", "p"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("parent/child", "c"), 2.0);
}

TEST_CASE("Deck2 - Math and expressions") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "n = 4\n"
     << "<ms>\n"
     << "sq = n * n\n"
     << "sr = sqrt(n)\n";
  d.Build(ss);
  FLOAT_REQUIRE(d.GetCardValue<double>("ms", "sq"), 16.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("ms", "sr"), 2.0);
}

TEST_CASE("Deck2 - DoesSuitExist and DoesCardExist") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<s>\nx = 1\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("s"));
  REQUIRE(!d.DoesSuitExist("missing"));
  REQUIRE(d.DoesCardExist("s", "x"));
  REQUIRE(!d.DoesCardExist("s", "missing"));
}

TEST_CASE("Deck2 - WriteDeck round-trip") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "g = 3\n"
     << "<s1>\n"
     << "a = g + 1\n"
     << "b = 2\n"
     << "<s2>\n"
     << "c = s1.a * b\n";
  d.Build(ss);

  std::ostringstream out;
  d.WriteDeck(out);

  Rummy::FullDeck d2;
  std::istringstream in(out.str());
  d2.Build(in);

  REQUIRE(d2.DoesSuitExist("s1"));
  REQUIRE(d2.DoesSuitExist("s2"));
  FLOAT_REQUIRE(d2.GetCardValue<double>("s1", "a"), 4.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("s1", "b"), 2.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("s2", "c"), 8.0);
}

TEST_CASE("Deck2 - FindSuitInOrder preserves source order") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<ord>\n"
     << "first = 1\n"
     << "second = 2\n"
     << "third = 3\n";
  d.Build(ss);
  auto cards = d.FindSuitInOrder("ord");
  REQUIRE(cards.size() == 3);
  REQUIRE(cards[0].name == "first");
  REQUIRE(cards[1].name == "second");
  REQUIRE(cards[2].name == "third");
}

TEST_CASE("Deck2 - Multiline continuation") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<ms>\n"
     << "x = 1 + &\n"
     << "    2\n";
  d.Build(ss);
  FLOAT_REQUIRE(d.GetCardValue<double>("ms", "x"), 3.0);
}

TEST_CASE("Deck2 - AddCard and UpdateCard API") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<s>\n"
     << "x = 1\n";
  d.Build(ss);

  d.AddCard("s", "y", 99.0, "added");
  FLOAT_REQUIRE(d.GetCardValue<double>("s", "y"), 99.0);
  REQUIRE(d.GetCard("s", "y").comment == "added");

  d.UpdateCard("s", "x", 42.0, "updated");
  FLOAT_REQUIRE(d.GetCardValue<double>("s", "x"), 42.0);
  REQUIRE(d.GetCard("s", "x").comment == "updated");
}

TEST_CASE("Deck2 - Inline comment stripped and preserved") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<s>\n"
     << "x = 5  # this is a comment\n";
  d.Build(ss);
  FLOAT_REQUIRE(d.GetCardValue<double>("s", "x"), 5.0);
  REQUIRE(d.GetCard("s", "x").comment == "this is a comment");
}

TEST_CASE("Deck2 - GetSuitsInOrder") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<aa>\nx = 1\n"
     << "<bb>\ny = 2\n"
     << "<cc>\nz = 3\n";
  d.Build(ss);
  auto order = d.GetSuitsInOrder();
  // "/" is first, then aa, bb, cc
  REQUIRE(order[0] == "/");
  REQUIRE(order[1] == "aa");
  REQUIRE(order[2] == "bb");
  REQUIRE(order[3] == "cc");
}

// ---------------------------------------------------------------------------
// New Deck2-specific tests: named instance syntax and readback
// ---------------------------------------------------------------------------

TEST_CASE("Deck2 - Named instance syntax <suit(name)>") {
  Rummy::FullDeck d;
  std::stringstream ss;
  // The alias `(p)` substitutes into the suit path; the VM variable, the
  // C++ suit path and the class name are derived as follows:
  //   suit path     -> "p"     (alias-substituted)
  //   VM variable   -> "p"     (alias)
  //   class name    -> "Params" (Capitalize of the node name)
  ss << "<params(p)>\n"
     << "x = 10\n"
     << "y = 20\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("p"));
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "x"), 10.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "y"), 20.0);
}

TEST_CASE("Deck2 - Named instance pips mode with cross-suit reference") {
  Rummy::FullDeck d;
  std::stringstream ss;
  // After alias-substitution the suit paths are "a" and "b".  Cross-suit
  // references must use the alias too.
  ss << "<alpha(a)>\n"
     << "val = 5\n"
     << "<beta(b)>\n"
     << "result = a.val * 3\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("a"));
  REQUIRE(d.DoesSuitExist("b"));
  FLOAT_REQUIRE(d.GetCardValue<double>("a", "val"), 5.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "result"), 15.0);
}

TEST_CASE("Deck2 - Readback discovers dynamic fields from for-loop") {
  Rummy::FullDeck d;
  d.Build(fixture("pips_mode_for_loop.par"));

  // Suit path is the alias-substituted "b" (from <loopblock(b)>).
  REQUIRE(d.DoesSuitExist("b"));

  // The for loop created v[0]=0, v[1]=10, v[2]=20
  REQUIRE(d.DoesCardExist("b", "v[0]"));
  REQUIRE(d.DoesCardExist("b", "v[1]"));
  REQUIRE(d.DoesCardExist("b", "v[2]"));

  FLOAT_REQUIRE(d.GetCardValue<double>("b", "v[0]"), 0.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "v[1]"), 10.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "v[2]"), 20.0);

  // Statically declared field also present
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "static_field"), 42.0);
}

TEST_CASE("Deck2 - Fixture: declarative_block.par") {
  Rummy::FullDeck d;
  d.Build(fixture("declarative_block.par"));

  REQUIRE(d.DoesSuitExist("myblock"));
  FLOAT_REQUIRE(d.GetCardValue<double>("myblock", "alpha"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("myblock", "beta"), 2.5);
  REQUIRE(d.GetCardValue<std::string>("myblock", "gamma") == "hello");
  REQUIRE(d.GetCardValue<bool>("myblock", "flag") == true);
  FLOAT_REQUIRE(d.GetCardValue<double>("myblock", "derived"), 99.0);
}

TEST_CASE("Deck2 - Fixture: declarative_vectors.par") {
  Rummy::FullDeck d;
  d.Build(fixture("declarative_vectors.par"));

  REQUIRE(d.DoesSuitExist("vecblock"));

  auto a = d.GetVector<double>("vecblock", "a");
  REQUIRE(a.size() == 3);
  FLOAT_REQUIRE(a[0], 10.0);
  FLOAT_REQUIRE(a[1], 20.0);
  FLOAT_REQUIRE(a[2], 30.0);

  auto b = d.GetVector<double>("vecblock", "b");
  REQUIRE(b.size() == 2);
  FLOAT_REQUIRE(b[0], 100.0);
  FLOAT_REQUIRE(b[1], 200.0);
}

TEST_CASE("Deck2 - Fixture: nested_declarative.par") {
  Rummy::FullDeck d;
  d.Build(fixture("nested_declarative.par"));

  REQUIRE(d.DoesSuitExist("parent"));
  REQUIRE(d.DoesSuitExist("parent/child"));
  FLOAT_REQUIRE(d.GetCardValue<double>("parent", "x"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("parent", "y"), 2.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("parent/child", "z"), 3.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("parent/child", "w"), 3.0); // parent.x + parent.y
}

TEST_CASE("Deck2 - Fixture: mixed_suits.par") {
  Rummy::FullDeck d;
  d.Build(fixture("mixed_suits.par"));

  REQUIRE(d.DoesSuitExist("alpha"));
  REQUIRE(d.DoesSuitExist("b2"));
  FLOAT_REQUIRE(d.GetCardValue<double>("alpha", "p"), 10.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("alpha", "q"), 20.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b2", "r"), 30.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b2", "s"), 30.0); // alpha.p + alpha.q
}

TEST_CASE("Deck2 - Fixture: include_root.par") {
  Rummy::FullDeck d;
  d.Build(fixture("include_root.par"));

  REQUIRE(d.DoesSuitExist("root_suit"));
  REQUIRE(d.DoesSuitExist("child_suit"));
  FLOAT_REQUIRE(d.GetCardValue<double>("root_suit", "root_field"), 14.0); // 7*2
  FLOAT_REQUIRE(d.GetCardValue<double>("child_suit", "child_val"), 99.0);
  REQUIRE(d.GetCardValue<std::string>("child_suit", "child_str") == "from_child");
}

TEST_CASE("Deck2 - Fixture: pips_function_in_globals.par") {
  Rummy::FullDeck d;
  d.Build(fixture("pips_function_in_globals.par"));

  REQUIRE(d.DoesSuitExist("funcsuit"));
  FLOAT_REQUIRE(d.GetCardValue<double>("funcsuit", "base"), 5.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("funcsuit", "doubled"), 10.0);
}

TEST_CASE("Deck2 - Fixture: globals_printer.par (smoke test)") {
  // Just verify it builds without crashing; the __globals__ statement prints
  // to stdout but is not tested here.
  Rummy::FullDeck d;
  d.Build(fixture("globals_printer.par"));
  REQUIRE(d.DoesSuitExist("gsuit"));
  FLOAT_REQUIRE(d.GetCardValue<double>("/", "g1"), 100.0);
  REQUIRE(d.GetCardValue<std::string>("/", "g2") == "world");
  FLOAT_REQUIRE(d.GetCardValue<double>("gsuit", "f1"), 101.0);
}

TEST_CASE("Deck2 - WriteDeck round-trip with named instance") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<params(p)>\n"
     << "x = 3\n"
     << "y = 4\n";
  d.Build(ss);

  std::ostringstream out;
  d.WriteDeck(out);

  // Re-parse the written output (without instance name — standard format)
  Rummy::FullDeck d2;
  std::istringstream in(out.str());
  d2.Build(in);

  // WriteDeck emits the alias-substituted suit path; re-parse preserves "p".
  REQUIRE(d2.DoesSuitExist("p"));
  FLOAT_REQUIRE(d2.GetCardValue<double>("p", "x"), 3.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("p", "y"), 4.0);
}

TEST_CASE("Deck2 - for-loop round-trip via WriteDeck") {
  // After readback, WriteDeck should produce output that re-parses correctly.
  Rummy::FullDeck d;
  d.Build(fixture("pips_mode_for_loop.par"));

  std::ostringstream out;
  d.WriteDeck(out);

  Rummy::FullDeck d2;
  std::istringstream in(out.str());
  d2.Build(in);

  REQUIRE(d2.DoesSuitExist("b"));
  FLOAT_REQUIRE(d2.GetCardValue<double>("b", "v[0]"), 0.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("b", "v[1]"), 10.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("b", "v[2]"), 20.0);
  FLOAT_REQUIRE(d2.GetCardValue<double>("b", "static_field"), 42.0);
}

// ---------------------------------------------------------------------------
// Pips control-flow coverage: each test exercises one construct in a
// pips-mode block (or in globals for fn / user class) and asserts that the
// resulting fields are visible via the standard readback.
// ---------------------------------------------------------------------------

TEST_CASE("Deck2 - if/else in pips-mode block") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "threshold = 10\n"
     << "<branch(b)>\n"
     << "if (threshold > 5) {\n"
     << "  setattr(b, \"taken\", 1)\n"
     << "  setattr(b, \"value\", 100)\n"
     << "} else {\n"
     << "  setattr(b, \"taken\", 0)\n"
     << "  setattr(b, \"value\", 200)\n"
     << "}\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("b"));
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "taken"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "value"), 100.0);
}

TEST_CASE("Deck2 - if/else with the else branch taken") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "threshold = 1\n"
     << "<branch(b)>\n"
     << "if (threshold > 5) {\n"
     << "  setattr(b, \"taken\", 1)\n"
     << "  setattr(b, \"value\", 100)\n"
     << "} else {\n"
     << "  setattr(b, \"taken\", 0)\n"
     << "  setattr(b, \"value\", 200)\n"
     << "}\n";
  d.Build(ss);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "taken"), 0.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("b", "value"), 200.0);
}

TEST_CASE("Deck2 - for-loop populates dynamic vector fields") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<grid(g)>\n"
     << "for (var i = 0; i < 4; i = i + 1) {\n"
     << "  setattr(g, \"x[\" + str(i) + \"]\", i * i)\n"
     << "}\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("g"));
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "x[0]"), 0.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "x[1]"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "x[2]"), 4.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "x[3]"), 9.0);
}

TEST_CASE("Deck2 - while-loop accumulates a sum") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<accum(a)>\n"
     << "var n = 1\n"
     << "var total = 0\n"
     << "while (n <= 5) {\n"
     << "  total = total + n\n"
     << "  n = n + 1\n"
     << "}\n"
     << "setattr(a, \"sum\", total)\n"
     << "setattr(a, \"count\", n - 1)\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("a"));
  FLOAT_REQUIRE(d.GetCardValue<double>("a", "sum"), 15.0); // 1+2+3+4+5
  FLOAT_REQUIRE(d.GetCardValue<double>("a", "count"), 5.0);
}

TEST_CASE("Deck2 - user-defined fn called from a pips-mode block") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "fn square(x) {\n"
     << "  return x * x\n"
     << "}\n"
     << "fn add(a, b) {\n"
     << "  return a + b\n"
     << "}\n"
     << "<calc(c)>\n"
     << "setattr(c, \"sq3\", square(3))\n"
     << "setattr(c, \"sq4\", square(4))\n"
     << "setattr(c, \"hyp_sq\", add(square(3), square(4)))\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("c"));
  FLOAT_REQUIRE(d.GetCardValue<double>("c", "sq3"), 9.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("c", "sq4"), 16.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("c", "hyp_sq"), 25.0);
}

TEST_CASE("Deck2 - user-defined class declared in globals, used in a block") {
  Rummy::FullDeck d;
  std::stringstream ss;
  // A user-defined pips class with a method, declared in globals.
  ss << "class Point {\n"
     << "  var x\n"
     << "  var y\n"
     << "  fn norm_sq() {\n"
     << "    return this.x * this.x + this.y * this.y\n"
     << "  }\n"
     << "}\n"
     << "<geom(g)>\n"
     << "var p = new Point { .x = 3; .y = 4 }\n"
     << "setattr(g, \"px\", p.x)\n"
     << "setattr(g, \"py\", p.y)\n"
     << "setattr(g, \"norm_sq\", p.norm_sq())\n";
  d.Build(ss);
  REQUIRE(d.DoesSuitExist("g"));
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "px"), 3.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "py"), 4.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("g", "norm_sq"), 25.0);
}

// ---------------------------------------------------------------------------
// New pips feature coverage: native vectors, user-declared classes, and
// device function packing/calling.
// ---------------------------------------------------------------------------

TEST_CASE("Deck2 - Native pips vector literal inside a block") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "<vsuit>\n"
     << "v = [1.5, 2.5, 3.5, 4.5]\n";
  d.Build(ss);
  REQUIRE(d.IsCardVector("vsuit", "v"));
  auto v = d.GetVector<double>("vsuit", "v");
  REQUIRE(v.size() == 4);
  FLOAT_REQUIRE(v[0], 1.5);
  FLOAT_REQUIRE(v[3], 4.5);
}

TEST_CASE("Deck2 - Vector with element expressions") {
  Rummy::FullDeck d;
  std::stringstream ss;
  ss << "n = 4\n"
     << "<v>\n"
     << "squares = [n * n, (n + 1) * (n + 1), (n + 2) * (n + 2)]\n";
  d.Build(ss);
  auto sq = d.GetVector<double>("v", "squares");
  REQUIRE(sq.size() == 3);
  FLOAT_REQUIRE(sq[0], 16.0);
  FLOAT_REQUIRE(sq[1], 25.0);
  FLOAT_REQUIRE(sq[2], 36.0);
}

TEST_CASE("Deck2 - Fixture: native_vectors.par") {
  Rummy::FullDeck d;
  d.Build(fixture("native_vectors.par"));

  REQUIRE(d.DoesSuitExist("vecops"));

  auto small = d.GetVector<double>("vecops", "small");
  REQUIRE(small.size() == 3);
  FLOAT_REQUIRE(small[0], 1.0);
  FLOAT_REQUIRE(small[2], 3.0);

  auto mixed = d.GetVector<double>("vecops", "mixed");
  REQUIRE(mixed.size() == 5);
  FLOAT_REQUIRE(mixed[4], 50.0);

  auto sliced = d.GetVector<double>("vecops", "sliced");
  REQUIRE(sliced.size() == 3);
  FLOAT_REQUIRE(sliced[1], 8.0);
}

TEST_CASE("Deck2 - User-declared class used via <Point(p)> header") {
  Rummy::FullDeck d;
  d.Build(fixture("user_class.par"));

  // The deck's globals declare `class Point { var x; var y; fn norm_sq() }`.
  // <Point(p)> should NOT trigger auto-class generation; instead it should
  // lower to `var p = new Point {}`. The instance name `p` is the suit
  // path; the class `Point` is recorded as metadata. The body populates
  // p.x and p.y and reads the user-declared method.
  REQUIRE(d.DoesSuitExist("p"));
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "x"), 3.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "y"), 4.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "nsq"), 25.0);
  REQUIRE(d.GetClassName("p") == "Point");
}

TEST_CASE("Deck2 - FindSuitsOfClass / GetClassName") {
  Rummy::FullDeck d;
  d.Build(fixture("class_query.par"));

  // One suit backed by user-declared class Point, one by Other.
  auto points = d.FindSuitsOfClass("Point");
  REQUIRE(points.size() == 1);
  REQUIRE(points[0] == "p");

  auto others = d.FindSuitsOfClass("Other");
  REQUIRE(others.size() == 1);
  REQUIRE(others[0] == "o");

  REQUIRE(d.FindSuitsOfClass("DoesNotExist").empty());

  // Each returned suit should report the queried class via GetClassName.
  REQUIRE(d.GetClassName("p") == "Point");
  REQUIRE(d.GetClassName("o") == "Other");
  REQUIRE(d.GetClassName("does/not/exist").empty());

  // The cards on each suit should match what the deck wrote.
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "x"), 3.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("p", "y"), 4.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("o", "k"), 99.0);
}

TEST_CASE("Deck2 - Multiple instances of same user class as distinct suits") {
  Rummy::FullDeck d;
  d.Build(fixture("gas_materials.par"));

  // Three <Gas(matN)> headers => three suits, all tagged class Gas.
  auto gases = d.FindSuitsOfClass("Gas");
  REQUIRE(gases.size() == 3);
  REQUIRE(gases[0] == "mat1");
  REQUIRE(gases[1] == "mat2");
  REQUIRE(gases[2] == "mat3");

  // Each suit is its own card store.
  FLOAT_REQUIRE(d.GetCardValue<double>("mat1", "rho"), 1.0);
  // mat1.T is now a vector [300, 100, 200] (multi-value RHS).
  REQUIRE(d.IsCardVector("mat1", "T"));
  auto mat1_T = d.GetVector<double>("mat1", "T");
  REQUIRE(mat1_T.size() == 3);
  FLOAT_REQUIRE(mat1_T[0], 300.0);
  FLOAT_REQUIRE(mat1_T[1], 100.0);
  FLOAT_REQUIRE(mat1_T[2], 200.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat2", "rho"), 2.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat2", "T"), 350.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat3", "rho"), 3.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("mat3", "T"), 400.0);

  // Class metadata flows back through GetClassName for every suit.
  for (const auto &sp : gases) REQUIRE(d.GetClassName(sp) == "Gas");
}

TEST_CASE("Deck2 - Device function pack + call: simple cube") {
  Rummy::FullDeck d;
  d.Build(fixture("device_functions.par"));

  // The deck registered `fn cube(x)`. After packing we should be able to
  // see it in the available list and call it on the device VM.
  std::string err;
  REQUIRE(d.PackDeviceFunction("cube", err));
  REQUIRE(err.empty());
  REQUIRE(d.HasDeviceFunction("cube"));

  auto names = d.AvailableDeviceFunctions();
  REQUIRE(std::find(names.begin(), names.end(), "cube") != names.end());

  FLOAT_REQUIRE(d.CallDeviceFunction("cube", {2.0}), 8.0);
  FLOAT_REQUIRE(d.CallDeviceFunction("cube", {-3.0}), -27.0);
  FLOAT_REQUIRE(d.CallDeviceFunction("cube", {0.0}), 0.0);
}

TEST_CASE("Deck2 - Device function pack + call: multi-arg poly") {
  Rummy::FullDeck d;
  d.Build(fixture("device_functions.par"));

  d.PackDeviceFunction("poly");
  // poly(a, b, c, x) = a*x*x + b*x + c
  FLOAT_REQUIRE(d.CallDeviceFunction("poly", {1.0, 0.0, 0.0, 5.0}), 25.0);
  FLOAT_REQUIRE(d.CallDeviceFunction("poly", {1.0, 2.0, 3.0, 4.0}),
                1.0 * 16 + 2.0 * 4 + 3.0);
  FLOAT_REQUIRE(d.CallDeviceFunction("poly", {0.0, 0.0, 7.0, 99.0}), 7.0);
}

TEST_CASE("Deck2 - Device function pack + call: control flow (clamp)") {
  Rummy::FullDeck d;
  d.Build(fixture("device_functions.par"));

  d.PackDeviceFunction("clamp");
  FLOAT_REQUIRE(d.CallDeviceFunction("clamp", {0.5, 0.0, 1.0}), 0.5);
  FLOAT_REQUIRE(d.CallDeviceFunction("clamp", {-1.0, 0.0, 1.0}), 0.0);
  FLOAT_REQUIRE(d.CallDeviceFunction("clamp", {2.0, 0.0, 1.0}), 1.0);
}

TEST_CASE("Deck2 - Device function handle is trivially copyable") {
  // The handle must be safe to capture by value into a device kernel
  // (e.g. a KOKKOS_LAMBDA), so it must be trivially copyable and small.
  STATIC_REQUIRE(std::is_trivially_copyable_v<Rummy::FullDeck::DeviceFunctionHandle>);
}

TEST_CASE("Deck2 - Device function not packed throws when accessed") {
  Rummy::FullDeck d;
  d.Build(fixture("device_functions.par"));
  REQUIRE_FALSE(d.HasDeviceFunction("cube"));
  REQUIRE_THROWS(d.GetDeviceFunction("cube"));
  REQUIRE_THROWS(d.CallDeviceFunction("cube", {1.0}));
}

TEST_CASE("Deck2 - PackDeviceFunction error path for unknown name") {
  Rummy::FullDeck d;
  d.Build(fixture("device_functions.par"));
  std::string err;
  REQUIRE_FALSE(d.PackDeviceFunction("not_a_function", err));
  REQUIRE_FALSE(err.empty());
  REQUIRE_FALSE(d.HasDeviceFunction("not_a_function"));
}

TEST_CASE("Deck2 - Relative header ./ anchors to last absolute suit") {
  // Multiple consecutive <./X> and <./X/Y> blocks must all resolve
  // relative to the same base suit (the last block opened with an absolute
  // path), not to each other.
  // ../ goes up one level, ../../ goes up two, etc.
  // YAML schema covering all classes used by this test.
  const std::string schema_yaml = R"(
gravity:
  _type: node
  _class: Gravity
  nbody:
    _type: node
    _class: Nbody
    integrator:
      _type: string
    particle:
      _type: node
      _class: Particle
      mass:
        _type: Real
      couple:
        _type: int
      soft:
        _type: node
        _class: Soft
        type:
          _type: string
      initialize:
        _type: node
        _class: Initialize
    binary:
      _type: node
      _class: Binary
      primary:
        _type: string
      secondary:
        _type: string
      a:
        _type: Real
      mass:
        _type: Real
)";
  Rummy::FullDeck d(Rummy::FullDeck::Mode::Strict,
                    Rummy::Schema::FromString(schema_yaml));
  std::stringstream ss;
  ss << "<gravity/nbody>\n"
     << "integrator = \"none\"\n"
     << "<./particle(star)>\n"
     << "mass = 1.0\n"
     << "couple = 1\n"
     << "<./particle(star)/soft>\n"
     << "type = \"none\"\n"
     << "<./particle(star)/initialize>\n"
     << "<./particle(planet)>\n"
     << "mass = 2.0\n"
     << "couple = 1\n"
     << "<./particle(planet)/soft>\n"
     << "type = \"none\"\n"
     << "<./binary>\n"
     << "primary = \"star\"\n"
     << "secondary = \"planet\"\n"
     << "a = 1.0\n"
     << "mass = 1.0\n";
  d.Build(ss);

  // The anchor must be gravity/nbody; all ./X blocks are children of it.
  REQUIRE(d.DoesSuitExist("gravity/nbody"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/star"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/star/soft"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/star/initialize"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/planet"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/planet/soft"));
  REQUIRE(d.DoesSuitExist("gravity/nbody/binary"));

  // Verify class names derived from node names (not aliases).
  REQUIRE(d.GetClassName("gravity/nbody/star") == "Particle");
  REQUIRE(d.GetClassName("gravity/nbody/planet") == "Particle");
  REQUIRE(d.GetClassName("gravity/nbody/star/soft") == "Soft");
  REQUIRE(d.GetClassName("gravity/nbody/binary") == "Binary");

  // Verify field values.
  REQUIRE(d.GetCardValue<std::string>("gravity/nbody", "integrator") == "none");
  FLOAT_REQUIRE(d.GetCardValue<double>("gravity/nbody/star", "mass"), 1.0);
  FLOAT_REQUIRE(d.GetCardValue<double>("gravity/nbody/planet", "mass"), 2.0);
  REQUIRE(d.GetCardValue<std::string>("gravity/nbody/star/soft", "type") == "none");
  REQUIRE(d.GetCardValue<std::string>("gravity/nbody/binary", "primary") == "star");
  FLOAT_REQUIRE(d.GetCardValue<double>("gravity/nbody/binary", "a"), 1.0);
}
