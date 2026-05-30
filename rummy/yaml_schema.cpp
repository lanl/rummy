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

// Hand-written YAML schema parser — intentionally narrow in scope:
//   * Indentation-based mappings (spaces only; tabs rejected)
//   * Scalar values (bare, double-quoted, numbers, bools)
//   * Inline lists `[a, b, c]` for `_allowed`
//   * `#` line comments (also recognised after a value)
//   * No anchors, flow-mappings, or block scalars (`|`, `>`)

#include "yaml_schema.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "rummy_utils.hpp"

namespace Rummy {

namespace {

// ---------------------------------------------------------------------------
// Generic YAML entry tree (produced by the parser, consumed by the schema
// builder).  Each entry is one key with either a direct scalar/list value or
// a list of nested children.
// ---------------------------------------------------------------------------
struct YamlEntry {
  std::string key;
  // Scalar value (if any).  Strings are stored *unquoted*; the
  // schema builder re-quotes when needed.
  std::string scalar;
  bool has_scalar = false;
  // Inline list `[a, b, c]` — only used for `_allowed` in this schema.
  std::vector<std::string> list;
  bool has_list = false;
  // Nested children (for mapping values).
  std::vector<YamlEntry> children;
  int indent = 0;
  int line_no = 0;
};

void TrimEdges(std::string &s) {
  RemoveLeadingWhitespace(s);
  RemoveTrailingWhitespace(s);
}

bool IsBlank(const std::string &s) {
  for (char c : s)
    if (!std::isspace(static_cast<unsigned char>(c))) return false;
  return true;
}

// Strip a `#` line comment that lies outside any double-quoted region.
void StripInlineComment(std::string &s) {
  bool in_quotes = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"') in_quotes = !in_quotes;
    else if (!in_quotes && c == '#') { s.resize(i); return; }
  }
}

// Strip surrounding double quotes if present.  Returns true if the input
// was a quoted string.
bool StripQuotes(std::string &s) {
  TrimEdges(s);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
    return true;
  }
  return false;
}

// Split an inline list `[a, b, "c with comma"]` body (without the brackets)
// into trimmed elements.  Quote-aware.
std::vector<std::string> SplitInlineList(const std::string &body) {
  std::vector<std::string> out;
  std::string cur;
  bool in_quotes = false;
  for (char c : body) {
    if (c == '"') { in_quotes = !in_quotes; cur += c; }
    else if (!in_quotes && c == ',') {
      TrimEdges(cur);
      StripQuotes(cur);
      out.push_back(cur);
      cur.clear();
    } else cur += c;
  }
  TrimEdges(cur);
  if (!cur.empty()) {
    StripQuotes(cur);
    out.push_back(cur);
  }
  return out;
}

[[noreturn]] void FatalSchema(const std::string &msg, int line_no) {
  std::stringstream m;
  m << "YAML schema error at line " << line_no << ": " << msg;
  fatal(m);
  throw std::runtime_error(m.str()); // unreachable; satisfies [[noreturn]]
}

// ---------------------------------------------------------------------------
// Parse the raw YAML text into a forest of YamlEntries (the top-level
// children of `root`).
// ---------------------------------------------------------------------------
void ParseYaml(const std::string &text, YamlEntry &root) {
  std::istringstream is(text);
  std::string line;
  int line_no = 0;
  // Indent-stack: each entry holds (indent, container). Children pushed
  // onto the container's `children` vector.
  struct Frame { int indent; YamlEntry *parent; };
  std::vector<Frame> stack;
  stack.push_back({-1, &root});

  while (std::getline(is, line)) {
    ++line_no;
    // Reject tabs in leading whitespace — keeps indentation unambiguous.
    for (char c : line) {
      if (c == ' ') continue;
      if (c == '\t') FatalSchema("tabs not allowed in indentation", line_no);
      break;
    }
    StripInlineComment(line);
    if (IsBlank(line)) continue;

    // Compute indent.
    int indent = 0;
    while (indent < static_cast<int>(line.size()) && line[indent] == ' ')
      ++indent;
    std::string body = line.substr(indent);

    // Find ':' separator (quote-aware).
    size_t colon = std::string::npos;
    {
      bool in_q = false;
      for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '"') in_q = !in_q;
        else if (!in_q && body[i] == ':') { colon = i; break; }
      }
    }
    if (colon == std::string::npos)
      FatalSchema("missing ':' in mapping line", line_no);

    std::string key = body.substr(0, colon);
    std::string rest = (colon + 1 < body.size()) ? body.substr(colon + 1) : "";
    TrimEdges(key);
    TrimEdges(rest);
    if (key.empty()) FatalSchema("empty key", line_no);

    // Pop until we find a frame whose indent < current indent.
    while (stack.size() > 1 && stack.back().indent >= indent) stack.pop_back();
    YamlEntry &parent = *stack.back().parent;

    YamlEntry entry;
    entry.key = key;
    entry.indent = indent;
    entry.line_no = line_no;

    if (rest.empty()) {
      // Mapping container; children come on subsequent indented lines.
    } else if (rest.front() == '[') {
      auto cb = rest.find_last_of(']');
      if (cb == std::string::npos)
        FatalSchema("missing ']' in inline list", line_no);
      std::string list_body = rest.substr(1, cb - 1);
      entry.list = SplitInlineList(list_body);
      entry.has_list = true;
    } else {
      // Scalar.  Drop optional surrounding quotes.
      StripQuotes(rest);
      entry.scalar = rest;
      entry.has_scalar = true;
    }

    parent.children.push_back(std::move(entry));
    YamlEntry *added = &parent.children.back();
    stack.push_back({indent, added});
  }
}

// ---------------------------------------------------------------------------
// Schema construction from the YamlEntry tree.
// ---------------------------------------------------------------------------
std::string Capitalize(const std::string &name) {
  if (name.empty()) return name;
  std::string out = name;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// True iff `e` looks like a leaf field (a `var` declaration on the parent
// class) rather than a child node (a sub-class instance).
//
// Discriminator (in priority order):
//   * `_type: node`  -> NODE (canonical sentinel for sub-class instances)
//   * `_class: ...`  -> NODE (explicit implementation-class name)
//   * `_type` matching a known primitive name -> LEAF
//   * `_default` or `_allowed` present -> LEAF
//   * any non-`_` child -> NODE
//   * otherwise LEAF (scalar/list-only)
bool IsLeafEntry(const YamlEntry &e) {
  bool has_class = false;
  bool has_default_or_allowed = false;
  std::string type_lo;
  for (const auto &c : e.children) {
    if (c.key == "_class") has_class = true;
    else if (c.key == "_default" || c.key == "_allowed") has_default_or_allowed = true;
    else if (c.key == "_type" && c.has_scalar) {
      type_lo = c.scalar;
      for (auto &ch : type_lo)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  if (type_lo == "node") return false;
  if (has_class) return false;
  static const std::set<std::string> primitives = {
      "real",   "int",    "integer", "number", "float", "double",
      "string", "str",    "bool",    "boolean"};
  if (primitives.count(type_lo)) return true;
  if (has_default_or_allowed) return true;
  for (const auto &c : e.children)
    if (c.key.empty() || c.key[0] != '_') return false;
  return true;
}

void BuildField(const YamlEntry &src, SchemaField &field) {
  for (const auto &meta : src.children) {
    if (meta.key == "_type" && meta.has_scalar) {
      field.type = meta.scalar;
    } else if (meta.key == "_description" && meta.has_scalar) {
      field.description = meta.scalar;
    } else if (meta.key == "_default") {
      if (meta.has_scalar) {
        field.default_value = meta.scalar;
        field.has_default = true;
      }
    } else if (meta.key == "_allowed" && meta.has_list) {
      field.allowed = meta.list;
    } else if (!meta.key.empty() && meta.key[0] == '_') {
      std::cerr << "rummy: yaml schema warning: unknown metadata key '"
                << meta.key << "' at line " << meta.line_no << "\n";
    }
  }
}

void BuildNode(const YamlEntry &src, SchemaNode &node) {
  // First pass: extract node-level metadata.
  for (const auto &meta : src.children) {
    if (meta.key == "_class" && meta.has_scalar) {
      node.class_name = meta.scalar;
    } else if (meta.key == "_description" && meta.has_scalar) {
      node.description = meta.scalar;
    }
    // `_type: node` is a sentinel and carries no other information.
  }
  if (node.class_name.empty()) node.class_name = Capitalize(src.key);

  // Second pass: classify children.
  for (const auto &child : src.children) {
    if (!child.key.empty() && child.key[0] == '_') continue; // metadata
    if (IsLeafEntry(child)) {
      SchemaField f;
      BuildField(child, f);
      node.field_order.push_back(child.key);
      node.fields[child.key] = std::move(f);
    } else {
      auto sub = std::make_shared<SchemaNode>();
      BuildNode(child, *sub);
      node.child_order.push_back(child.key);
      node.children[child.key] = std::move(sub);
    }
  }
}

// ---------------------------------------------------------------------------
// EmitClassDefs helpers.
// ---------------------------------------------------------------------------

// Format a default value into a pips literal based on `type`.  When `type`
// indicates a string, wrap the raw text in double quotes (and escape any
// embedded `"`).  Otherwise pass through verbatim.
std::string FormatDefault(const std::string &type,
                          const std::string &default_text) {
  std::string ty = type;
  for (auto &c : ty) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  bool is_string = (ty == "string" || ty == "str");
  if (!is_string) return default_text;
  std::string out;
  out.reserve(default_text.size() + 2);
  out += '"';
  for (char c : default_text) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

void CollectClassDefs(const SchemaNode &node,
                      std::set<std::string> &emitted,
                      std::ostringstream &os) {
  if (!node.class_name.empty() && emitted.insert(node.class_name).second) {
    os << "class " << node.class_name << " {\n";
    // Fields first.
    for (const auto &fname : node.field_order) {
      const auto &f = node.fields.at(fname);
      os << "  var " << fname;
      if (f.has_default) {
        os << " = " << FormatDefault(f.type, f.default_value);
      }
      os << "\n";
    }
    // Child node names also become fields of this class so the parent
    // class can hold the child instance via `parent.child = new Child {}`.
    for (const auto &cname : node.child_order) {
      os << "  var " << cname << "\n";
    }
    os << "}\n";
  }
  for (const auto &cname : node.child_order) {
    CollectClassDefs(*node.children.at(cname), emitted, os);
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Schema Schema::FromString(const std::string &text) {
  Schema s;
  YamlEntry root;
  ParseYaml(text, root);
  for (const auto &top : root.children) {
    if (!top.key.empty() && top.key[0] == '_') continue; // top-level meta
    auto node = std::make_shared<SchemaNode>();
    BuildNode(top, *node);
    s.root_->child_order.push_back(top.key);
    s.root_->children[top.key] = std::move(node);
  }
  return s;
}

Schema Schema::FromFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::stringstream m;
    m << "Cannot open YAML schema '" << path << "'";
    fatal(m);
  }
  std::stringstream ss; ss << in.rdbuf();
  return FromString(ss.str());
}

const SchemaNode *Schema::Lookup(const std::string &suit_path) const {
  if (suit_path.empty() || suit_path == "/") return root_.get();
  const SchemaNode *cur = root_.get();
  size_t pos = 0;
  while (pos <= suit_path.size()) {
    size_t slash = suit_path.find('/', pos);
    std::string seg = suit_path.substr(
        pos, slash == std::string::npos ? std::string::npos : slash - pos);
    if (seg.empty()) break;
    auto it = cur->children.find(seg);
    if (it == cur->children.end()) return nullptr;
    cur = it->second.get();
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }
  return cur;
}

std::string Schema::ClassFor(const std::string &suit_path) const {
  const auto *n = Lookup(suit_path);
  return n ? n->class_name : std::string();
}

bool Schema::ValidateAllowed(const std::string &suit_path,
                             const std::string &field_name,
                             const std::string &value_text,
                             std::string &err) const {
  const auto *n = Lookup(suit_path);
  if (!n) return true; // unknown suit — let other validators speak first
  auto it = n->fields.find(field_name);
  if (it == n->fields.end()) return true; // unknown field — same
  const auto &f = it->second;
  if (f.allowed.empty()) return true;
  // Compare value verbatim and also with surrounding double-quotes stripped,
  // since deck values for strings are quoted.
  std::string v = value_text;
  TrimEdges(v);
  std::string v_unquoted = v;
  StripQuotes(v_unquoted);
  for (const auto &a : f.allowed) {
    if (v == a || v_unquoted == a) return true;
  }
  std::ostringstream m;
  m << "value " << v << " for field '" << field_name << "' on suit '"
    << suit_path << "' is not in allowed set [";
  for (size_t i = 0; i < f.allowed.size(); ++i) {
    if (i) m << ", ";
    m << f.allowed[i];
  }
  m << "]";
  err = m.str();
  return false;
}

std::string Schema::EmitClassDefs() const {
  std::ostringstream os;
  std::set<std::string> emitted;
  for (const auto &cname : root_->child_order) {
    CollectClassDefs(*root_->children.at(cname), emitted, os);
  }
  return os.str();
}

} // namespace Rummy
