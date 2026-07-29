//========================================================================================
// (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
//========================================================================================

// This file was created in part with generative AI

#include "deck_graph.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#include <pips/value.hpp>
#include <pips/value_types.hpp>

namespace Rummy {

// ---------------------------------------------------------------------------
// DeckNode queries
// ---------------------------------------------------------------------------
std::vector<const DeckNode *> DeckNode::ChildList() const {
  std::vector<const DeckNode *> out;
  out.reserve(children.size());
  for (const auto &c : children) out.push_back(c.get());
  return out;
}

std::vector<const DeckNode *> DeckNode::Siblings() const {
  std::vector<const DeckNode *> out;
  if (!parent) return out;
  for (const auto &c : parent->children) {
    if (c.get() != this) out.push_back(c.get());
  }
  return out;
}

std::vector<const DeckNode *> DeckNode::Descendants() const {
  std::vector<const DeckNode *> out;
  std::function<void(const DeckNode *)> dfs = [&](const DeckNode *n) {
    for (const auto &c : n->children) {
      out.push_back(c.get());
      dfs(c.get());
    }
  };
  dfs(this);
  return out;
}

const DeckNode *DeckNode::FindByName(const std::string &n) const {
  if (name == n && kind != NodeKind::Root) return this;
  for (const auto &c : children) {
    if (auto *hit = c->FindByName(n)) return hit;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Value -> pips literal (matches the lowering convention used by FullDeck so
// the saved file round-trips exactly).
// ---------------------------------------------------------------------------
static std::string value_to_literal(const pips::Value &v) {
  std::ostringstream os;
  switch (v.type) {
    case pips::ValueType::STRING: {
      const char *s = AS_STRING(v);
      os << "\"" << (s ? s : "") << "\"";
      break;
    }
    case pips::ValueType::BOOL:
      os << (v.as.boolean ? "true" : "false");
      break;
    case pips::ValueType::NUMBER:
      os << std::setprecision(std::numeric_limits<double>::max_digits10)
         << v.as.number;
      break;
    case pips::ValueType::NIL:
      os << "nil";
      break;
    case pips::ValueType::VECTOR: {
      os << "[";
      auto *vo = AS_VECTOR(v);
      if (vo) {
        for (size_t i = 0; i < vo->elements.size(); ++i) {
          if (i) os << ", ";
          os << value_to_literal(vo->elements[i]);
        }
      }
      os << "]";
      break;
    }
    case pips::ValueType::INSTANCE:
      // Instances must be emitted by Save as `new Class { ... }`, not via
      // value_to_literal; falling here means the graph is malformed.
      os << "nil";
      break;
  }
  return os.str();
}

// ---------------------------------------------------------------------------
// Print
// ---------------------------------------------------------------------------
void DeckGraph::Print(std::ostream &os) const {
  std::function<void(const DeckNode *, int)> rec = [&](const DeckNode *n,
                                                       int depth) {
    std::string indent(depth * 2, ' ');
    switch (n->kind) {
      case NodeKind::Root:
        os << "root\n";
        break;
      case NodeKind::Class:
        os << indent << n->name << " : " << n->class_name << "\n";
        break;
      case NodeKind::Variable:
        os << indent << n->name << " = " << value_to_literal(n->value) << "\n";
        break;
    }
    for (const auto &c : n->children) rec(c.get(), depth + 1);
  };
  rec(&Root(), 0);
}

// ---------------------------------------------------------------------------
// Save: emit a self-contained pips program
// ---------------------------------------------------------------------------
namespace {

// Walk the graph and collect every distinct class definition referenced by
// a Class node, in first-seen order so the emitted file is deterministic.
void collect_classes(const DeckNode *n,
                     std::vector<pips::ClassDef *> &order,
                     std::set<pips::ClassDef *> &seen) {
  if (n->kind == NodeKind::Class && n->class_def &&
      seen.insert(n->class_def).second) {
    order.push_back(n->class_def);
  }
  for (const auto &c : n->children) collect_classes(c.get(), order, seen);
}

// Recursively write `new Cls { .field = ...; .child = new ...; ... }`.
void write_instance_initializer(const DeckNode *inst, std::ostream &os) {
  os << "new " << inst->class_name << " {";
  for (const auto &child : inst->children) {
    os << " ." << child->name << " = ";
    if (child->kind == NodeKind::Variable) {
      os << value_to_literal(child->value);
    } else {
      write_instance_initializer(child.get(), os);
    }
    os << ";";
  }
  if (!inst->children.empty()) os << " ";
  os << "}";
}

} // namespace

void DeckGraph::Save(std::ostream &os) const {
  // Emit class declarations (deduped, in first-seen order).
  std::vector<pips::ClassDef *> classes;
  std::set<pips::ClassDef *> seen;
  collect_classes(&Root(), classes, seen);
  for (auto *cd : classes) {
    os << "class " << cd->name << " {";
    for (const auto &f : cd->fields) {
      os << " var " << f << ";";
    }
    if (!cd->fields.empty()) os << " ";
    os << "}\n";
  }
  if (!classes.empty()) os << "\n";

  // Emit each top-level instance/variable.  Root is never written.
  for (const auto &child : Root().children) {
    if (child->kind == NodeKind::Class) {
      os << "var " << child->name << " = ";
      write_instance_initializer(child.get(), os);
      os << "\n";
    } else if (child->kind == NodeKind::Variable) {
      os << "var " << child->name << " = "
         << value_to_literal(child->value) << "\n";
    }
  }
}

} // namespace Rummy
