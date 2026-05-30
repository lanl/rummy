//========================================================================================
// (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
//========================================================================================

// Tests for FullDeck's linked-tree (DeckGraph) representation: BuildGraph,
// SaveGraph, LoadGraph, PrintGraph, and the per-node relationship queries.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "deck_graph.hpp"
#include "full_deck.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

#define FLOAT_REQUIRE(a, b) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 1e-10))

static std::string fixture(const std::string &name) {
  return std::string(RUMMY_TEST_INPUT_DIR) + "/" + name;
}

static std::string slurp(const std::string &path) {
  std::ifstream in(path);
  std::stringstream ss; ss << in.rdbuf();
  return ss.str();
}

// Locate a top-level child by name, asserting it exists.
static const Rummy::DeckNode *child_named(const Rummy::DeckNode &n,
                                          const std::string &nm) {
  for (const auto &c : n.children) {
    if (c->name == nm) return c.get();
  }
  return nullptr;
}

// Build a small two-material deck used by most of the test cases.
static std::unique_ptr<Rummy::FullDeck> make_gas_deck() {
  const std::string defs = slurp(fixture("strict_classes.par"));
  auto d = std::make_unique<Rummy::FullDeck>(Rummy::FullDeck::Mode::Strict,
                                              defs);
  d->Build(fixture("strict_gas.par"));
  return d;
}

TEST_CASE("DeckGraph - basic topology") {
  auto d = make_gas_deck();
  Rummy::DeckGraph g = d->BuildGraph();
  const auto &root = g.Root();
  REQUIRE(root.kind == Rummy::NodeKind::Root);
  REQUIRE(root.name.empty());

  // Two materials -> two top-level Class children.
  auto *mat1 = child_named(root, "mat1");
  auto *mat2 = child_named(root, "mat2");
  REQUIRE(mat1 != nullptr);
  REQUIRE(mat2 != nullptr);
  REQUIRE(mat1->kind == Rummy::NodeKind::Class);
  REQUIRE(mat1->class_name == "Gas");
  REQUIRE(mat2->class_name == "Gas");

  // mat1 should have rho (Variable), T (Variable/vector), eos (Class).
  auto *rho = child_named(*mat1, "rho");
  auto *t   = child_named(*mat1, "T");
  auto *eos = child_named(*mat1, "eos");
  REQUIRE(rho != nullptr);
  REQUIRE(t   != nullptr);
  REQUIRE(eos != nullptr);
  REQUIRE(rho->kind == Rummy::NodeKind::Variable);
  REQUIRE(t->kind   == Rummy::NodeKind::Variable);
  REQUIRE(eos->kind == Rummy::NodeKind::Class);
  REQUIRE(eos->class_name == "Eos");

  // Names are true pips instance names (not class names).
  REQUIRE(mat1->name == "mat1");
  REQUIRE(eos->name  == "eos");
}

TEST_CASE("DeckGraph - parent / sibling / descendant queries") {
  auto d = make_gas_deck();
  Rummy::DeckGraph g = d->BuildGraph();

  const auto *mat1 = child_named(g.Root(), "mat1");
  REQUIRE(mat1 != nullptr);
  // Parent of mat1 is the Root node.
  REQUIRE(mat1->parent == &g.Root());

  // Siblings of mat1 = every other top-level child = {mat2}.
  auto sibs = mat1->Siblings();
  REQUIRE(sibs.size() == 1);
  REQUIRE(sibs[0]->name == "mat2");

  // eos's parent is mat1.
  const auto *eos = child_named(*mat1, "eos");
  REQUIRE(eos->parent == mat1);

  // Descendants of mat1 = {rho, T, eos, type, gamma} (order: rho,T,eos,...).
  auto desc = mat1->Descendants();
  std::vector<std::string> names;
  for (auto *n : desc) names.push_back(n->name);
  REQUIRE(std::find(names.begin(), names.end(), "rho")   != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "T")     != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "eos")   != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "type")  != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "gamma") != names.end());

  // Top-level FindByName from the graph (DFS may return either mat's eos
  // depending on global iteration order, so just assert it found one
  // child of a Gas instance).
  const auto *found = g.Find("eos");
  REQUIRE(found != nullptr);
  REQUIRE(found->kind == Rummy::NodeKind::Class);
  REQUIRE(found->parent != nullptr);
  REQUIRE(found->parent->class_name == "Gas");
}

TEST_CASE("DeckGraph - PrintGraph produces nested output") {
  auto d = make_gas_deck();
  std::ostringstream os;
  d->PrintGraph(os);
  const std::string s = os.str();
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("root"));
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("mat1 : Gas"));
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("eos : Eos"));
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("rho = 1"));
}

TEST_CASE("DeckGraph - Save emits pips creation syntax (no root)") {
  auto d = make_gas_deck();
  std::ostringstream os;
  d->SaveGraph(os);
  const std::string s = os.str();
  // Class declarations come first.
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("class Gas"));
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("class Eos"));
  // Top-level instance creation using `var x = new Class { ... }`.
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring("var mat1 = new Gas {"));
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring(".rho = 1"));
  // Vector field is emitted as a single pips vector literal.
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring(".T = [300"));
  // Child instance uses nested `new` (no setattr).
  REQUIRE_THAT(s, Catch::Matchers::ContainsSubstring(".eos = new Eos {"));
  // Root must never appear in the saved output.
  REQUIRE_THAT(s, !Catch::Matchers::ContainsSubstring("root"));
}

TEST_CASE("DeckGraph - Save round-trips through Build") {
  auto src = make_gas_deck();
  std::ostringstream saved;
  src->SaveGraph(saved);

  // Loading a saved file is just executing pips, so a default-mode FullDeck
  // works: the file is self-contained (class decls + instance creations).
  Rummy::FullDeck reloaded;
  std::istringstream is(saved.str());
  reloaded.LoadGraph(is);

  // Same scalar values survive the round-trip.
  FLOAT_REQUIRE(reloaded.GetCardValue<double>("mat1", "rho"),
                src->GetCardValue<double>("mat1", "rho"));
  FLOAT_REQUIRE(reloaded.GetCardValue<double>("mat2", "rho"),
                src->GetCardValue<double>("mat2", "rho"));
  FLOAT_REQUIRE(reloaded.GetCardValue<double>("mat1/eos", "gamma"),
                src->GetCardValue<double>("mat1/eos", "gamma"));
  REQUIRE(reloaded.GetCardValue<std::string>("mat1/eos", "type") ==
          src->GetCardValue<std::string>("mat1/eos", "type"));

  // Vector field too.
  REQUIRE(reloaded.IsCardVector("mat1", "T"));
  auto a = src->GetVector<double>("mat1", "T");
  auto b = reloaded.GetVector<double>("mat1", "T");
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) FLOAT_REQUIRE(a[i], b[i]);

  // And the rebuilt graph has the same topology.
  auto g2 = reloaded.BuildGraph();
  const auto *mat1 = child_named(g2.Root(), "mat1");
  REQUIRE(mat1 != nullptr);
  REQUIRE(mat1->class_name == "Gas");
  REQUIRE(child_named(*mat1, "eos") != nullptr);
}

TEST_CASE("DeckGraph - Save preserves top-level source order") {
  std::stringstream defs;
  defs << "class Pc { var c; }\n"
       << "class Mesh { var nx1; }\n"
       << "class Physics { var enabled; }\n";

  std::stringstream deck;
  deck << "dt = 1.0e-3\n\n"
    << "<pc>\n"
       << "c = 3.0e10\n\n"
       << "<mesh>\n"
       << "nx1 = 64\n\n"
       << "<physics>\n"
       << "enabled = true\n";

  Rummy::FullDeck d(Rummy::FullDeck::Mode::Strict, defs.str());
  d.Build(deck);

  std::ostringstream os;
  d.SaveGraph(os);
  const std::string s = os.str();

  const auto pc_pos = s.find("var pc = new Pc {");
  const auto dt_pos = s.find("var dt = 0.001");
  const auto mesh_pos = s.find("var mesh = new Mesh {");
  const auto physics_pos = s.find("var physics = new Physics {");
  REQUIRE(pc_pos != std::string::npos);
  REQUIRE(dt_pos != std::string::npos);
  REQUIRE(mesh_pos != std::string::npos);
  REQUIRE(physics_pos != std::string::npos);
  REQUIRE(dt_pos < pc_pos);
  REQUIRE(pc_pos < mesh_pos);
  REQUIRE(mesh_pos < physics_pos);
}
