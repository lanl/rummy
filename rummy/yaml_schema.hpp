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

#ifndef RUMMY_YAML_SCHEMA_HPP_
#define RUMMY_YAML_SCHEMA_HPP_

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Rummy {

// A single leaf field in the schema (variable on a node's class).
struct SchemaField {
  std::string type;          // "_type" — e.g. "Real", "string", "int", "bool"
  std::string description;   // "_description"
  std::string default_value; // "_default" — raw text from YAML (no quoting)
  bool has_default = false;
  std::vector<std::string> allowed; // "_allowed" — list of permitted values
};

// A schema node corresponds to a suit (a `<...>` block in the deck).
// Children are sub-suits; fields are the variables in the node's class.
struct SchemaNode {
  std::string class_name;  // resolved from `_type` (or Capitalize(key) fallback)
  std::string description; // "_description"
  // Fields and children both appear under the same YAML mapping; we
  // distinguish them syntactically: any sub-mapping that has a `_type`
  // beginning with "class " (or contains nested children) is a node;
  // everything else is a leaf field.
  std::vector<std::string> field_order;
  std::map<std::string, SchemaField> fields;
  std::vector<std::string> child_order;
  std::map<std::string, std::shared_ptr<SchemaNode>> children;
};

// Top-level schema container.  The root has no class itself; its children
// are the top-level suits.
class Schema {
 public:
  Schema() = default;

  static Schema FromString(const std::string &text);
  static Schema FromFile(const std::string &path);

  // Look up a node by slash-separated suit path (e.g. "gravity/nbody").
  // Returns nullptr if the path is not present.
  const SchemaNode *Lookup(const std::string &suit_path) const;

  // Return the class name for a given suit path, or empty string if the
  // path is not in the schema.
  std::string ClassFor(const std::string &suit_path) const;

  // Validate that `value_text` (the raw RHS text from the deck) is one of
  // the allowed values for the field at suit_path/field_name.  If the
  // schema has no `_allowed` for that field, this is a no-op success.
  // On failure, writes a human-readable reason into `err`.
  bool ValidateAllowed(const std::string &suit_path,
                       const std::string &field_name,
                       const std::string &value_text,
                       std::string &err) const;

  // Generate pips `class Foo { var x = <default>; ... }` declarations for
  // every node reachable from the root, deduplicated by class name.  The
  // emitted text is suitable for prepending to the lowered deck program.
  std::string EmitClassDefs() const;

  // True iff the schema has any nodes.  (FromString("") returns an empty
  // Schema for which Empty() is true.)
  bool Empty() const { return root_->children.empty(); }

  const SchemaNode &Root() const { return *root_; }

 private:
  std::shared_ptr<SchemaNode> root_ = std::make_shared<SchemaNode>();
};

} // namespace Rummy

#endif // RUMMY_YAML_SCHEMA_HPP_
