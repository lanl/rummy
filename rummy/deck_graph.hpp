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

#ifndef RUMMY_DECK_GRAPH_HPP_
#define RUMMY_DECK_GRAPH_HPP_

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <pips/object.hpp>
#include <pips/value.hpp>

namespace Rummy {

// Kind of a node in a DeckGraph.
//   Root     - synthetic top-level node owning every top-level instance.
//              Has no pips analogue and is NEVER emitted by Save().
//   Class    - a pips Instance.  `class_name` holds the class name; the
//              node's children are its fields (variable nodes) and any
//              sub-instances.
//   Variable - a non-instance field of a pips Instance (or a non-instance
//              global).  `value` holds the live pips value.
enum class NodeKind { Root, Class, Variable };

// A node in the deck graph.
//
// Tree topology is owned by the children vector; sibling relationships
// are implicit (every node with the same parent is a sibling).  Each
// node knows its parent so upward queries are O(1).
struct DeckNode {
  NodeKind kind = NodeKind::Root;
  // The pips instance name (variable name in the VM) for this node.
  // For variables, this is the field name.  Empty for the root.
  std::string name;
  // Class name for NodeKind::Class nodes; empty otherwise.
  std::string class_name;
  // Pointer to the live pips class definition, used by Save() to emit
  // declarations. May be null if the class was synthetic or no longer
  // resident in the source VM.
  pips::ClassDef *class_def = nullptr;
  // Live pips value for NodeKind::Variable nodes.  Unused for Class/Root.
  pips::Value value{};

  DeckNode *parent = nullptr;
  std::vector<std::unique_ptr<DeckNode>> children;

  // ----- Relationship queries ----------------------------------------
  // First-level children only (immediate descendants).
  std::vector<const DeckNode *> ChildList() const;
  // Sibling nodes (other children of the same parent; this node excluded).
  std::vector<const DeckNode *> Siblings() const;
  // Every descendant in depth-first pre-order (this node excluded).
  std::vector<const DeckNode *> Descendants() const;
  // First descendant (depth-first) with the matching name, or nullptr.
  const DeckNode *FindByName(const std::string &name) const;
};

// A whole-deck linked tree of instances and their fields.
//
// Build via FullDeck::BuildGraph().  The DeckGraph is a snapshot — it
// holds non-owning pointers into the FullDeck's pips VM (via classDef
// pointers and pips::Value references to live VectorObjects / strings),
// so it must not outlive the FullDeck it was built from.
class DeckGraph {
 public:
  DeckGraph() : root_(std::make_unique<DeckNode>()) {
    root_->kind = NodeKind::Root;
  }

  // Read-only root accessor.
  const DeckNode &Root() const { return *root_; }
  // Mutable root accessor (used by FullDeck during BuildGraph).
  DeckNode &MutableRoot() { return *root_; }

  // First node anywhere in the graph with this name (depth-first), or
  // nullptr if none.
  const DeckNode *Find(const std::string &name) const {
    return root_->FindByName(name);
  }

  // Pretty-print the tree to `os`.  Variables are shown as "name = value",
  // class nodes as "name : ClassName".
  void Print(std::ostream &os) const;

  // Emit the graph as a pips program using `new Class { .field = expr; ... }`
  // syntax.  Class declarations are emitted first (for every class
  // referenced in the graph), then `var <name> = new Cls { ... }` lines
  // for every top-level instance.  The synthetic Root node is NOT
  // written.  Re-loading the output via FullDeck::LoadGraph (or any
  // FullDeck::Build) reconstructs an equivalent deck.
  void Save(std::ostream &os) const;

 private:
  std::unique_ptr<DeckNode> root_;
};

} // namespace Rummy

#endif // RUMMY_DECK_GRAPH_HPP_
