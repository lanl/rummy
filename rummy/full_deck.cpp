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

// Deck2 — block-aware front end that lowers a deck source to ONE pips program
// (schema classes + per-block lowering) and reads cards back by walking the
// VM instance graph after a single vm.interpret() call.
//
// Pipeline:
//   parse  -> Block IR (per-suit, brace-balanced raw lines)
//   classify -> Declarative vs Pips mode per block
//   schema  -> class RummySuit_X { var f1; var f2; ... } per suit
//   emit    -> globals + per-block lowering into one std::string
//   compile -> vm.interpret(program) ONCE
//   readback -> walk vm.globals + instance graph -> populate deck table

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "full_deck.hpp"
#include "rummy_utils.hpp"
#include <pips/value.hpp>
#include <pips/vm.hpp>

namespace Rummy {

namespace {

// --------------------------------------------------------------------------
// Suit / class name mangling
// --------------------------------------------------------------------------
const std::set<std::string> kPipsReserved = {
    "and",   "class",  "else",  "false", "for",    "fn",   "if",      "nil",
    "not",   "new",    "or",    "print", "return", "super", "this",   "true",
    "var",   "while",  "getattr", "setattr",
    "pi",    "min",    "max",   "exp",   "sin",    "cos",  "tan",    "abs",
    "log",   "log10",  "sign",  "sqrt",  "acos",   "asin", "atan",   "atan2",
    "ceil",  "floor",  "env",   "str"};

bool NeedsVmSuitAlias(const std::string &segment) {
  if (segment.empty() || kPipsReserved.count(segment) > 0) return true;
  if (!(std::isalpha(static_cast<unsigned char>(segment.front())) ||
        segment.front() == '_'))
    return true;
  return std::any_of(segment.begin(), segment.end(), [](char c) {
    return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_');
  });
}

std::string VmSuitSegment(const std::string &segment) {
  if (!NeedsVmSuitAlias(segment)) return segment;
  // Reserved word or non-identifier: prefix with single underscore to keep
  // it valid pips while staying readable in generated output.
  std::string aliased = "_";
  for (char c : segment) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') aliased += c;
    else aliased += '_';
  }
  return aliased;
}

std::vector<std::string> SplitSuitPath(const std::string &suit_path) {
  std::vector<std::string> parts;
  if (suit_path.empty() || suit_path == "/") return parts;
  std::stringstream ss(suit_path);
  std::string part;
  while (std::getline(ss, part, '/'))
    if (!part.empty()) parts.push_back(part);
  return parts;
}

std::string ExternalSuitDotPath(const std::string &suit_path) {
  std::string out = suit_path;
  std::replace(out.begin(), out.end(), '/', '.');
  return out;
}

// Class name for a suit path. Uses the literal suit identifier(s) joined by
// '_' so that `<X>` lowers to `class X { ... }; var X = new X()` and a
// user-declared `class X { ... }` in the deck source is used directly.
std::string SuitClassName(const std::string &suit_path) {
  std::string cn;
  for (char c : suit_path) {
    if (std::isalnum(static_cast<unsigned char>(c))) cn += c;
    else cn += '_';
  }
  return cn;
}

// Capitalize the first character of `name` (leave the remainder unchanged).
// Used to derive the synthesised class name from a `<node>` header.
std::string Capitalize(const std::string &name) {
  if (name.empty()) return name;
  std::string out = name;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// Count unmatched braces in a string (quote-aware). >0 means more '{'.
int CountBraceDepth(const std::string &s) {
  int d = 0;
  bool in_quotes = false;
  for (char c : s) {
    if (c == '"') in_quotes = !in_quotes;
    else if (!in_quotes && c == '{') ++d;
    else if (!in_quotes && c == '}') --d;
  }
  return d;
}

// --------------------------------------------------------------------------
// Deck IR
// --------------------------------------------------------------------------
struct DeckLine {
  int loc;             // source line number (first physical line)
  std::string raw;     // joined text (multi-line & + brace-balanced groups)
  std::string comment; // trailing comment text (without leading '#')
};

struct BlockHeader {
  std::string suit_path;       // "" for the implicit globals block. For
                               // non-globals this is the EFFECTIVE path:
                               // each segment's alias (or the node name if
                               // no alias) joined by '/'.
  std::string instance_name;   // last-segment alias; empty if none
  // Per-segment original node names (one entry per '/'-separated segment).
  // Used to derive class names: Capitalize(node_names[i]) is the class for
  // the i-th segment.  Empty for the globals block.
  std::vector<std::string> node_names;
  int loc = 0;
};

enum class BlockMode { Declarative, Pips };

struct Block {
  BlockHeader header;
  std::vector<DeckLine> lines;
  BlockMode mode = BlockMode::Declarative;
};

struct DeckIR {
  std::vector<Block> blocks; // blocks[0] is always the globals block ("")
  void print() {
    for (const auto &b : blocks) {
      std::cout << "Block: suit_path='" << b.header.suit_path
                << "' instance_name='" << b.header.instance_name
                << "' loc=" << b.header.loc
                << " mode=" << (b.mode == BlockMode::Declarative ? "Declarative" : "Pips")
                << std::endl;
      for (const auto &l : b.lines) {
        std::cout << "  Line " << l.loc << ": '" << l.raw << "'";
        if (!l.comment.empty()) std::cout << "  # " << l.comment;
        std::cout << std::endl;
      }
    }
  }
};

// --------------------------------------------------------------------------
// IR parse — read the stream and produce a flat, ordered DeckIR.
// --------------------------------------------------------------------------
class IrParser {
 public:
  void Parse(std::istream &ss, const std::string &base_dir,
             std::set<std::string> &include_stack, DeckIR &ir) {
    if (ir.blocks.empty()) {
      ir.blocks.push_back(Block{BlockHeader{"", "", {}, 0}, {}, BlockMode::Declarative});
      current_block_ = 0;
    }
    std::string line;
    int line_num = 0;
    // multi-line & continuation
    std::string cont_buf;
    std::string cont_comment;
    int cont_start_loc = 0;
    bool in_continuation = false;
    // brace accumulation
    std::string brace_buf;
    std::string brace_comment;
    int brace_start_loc = 0;
    int brace_depth = 0;

    while (std::getline(ss, line)) {
      ++line_num;
      // strip CR / other control characters except plain space
      line.erase(std::remove_if(line.begin(), line.end(),
                                [](char c) { return std::isspace(c) && c != ' '; }),
                 line.end());
      if (line.empty()) {
        if (brace_depth > 0) brace_buf += "\n";
        continue;
      }

      auto first = line.find_first_not_of(' ');
      if (first == std::string::npos) {
        if (brace_depth > 0) brace_buf += "\n";
        continue;
      }
      if (line[first] == '#') continue; // whole-line comment

      // Strip inline comment (quote-aware), preserving '#' inside quotes.
      std::string this_comment;
      {
        bool in_quotes = false;
        size_t cpos = std::string::npos;
        for (size_t i = 0; i < line.size(); ++i) {
          if (line[i] == '"') in_quotes = !in_quotes;
          else if (line[i] == '#' && !in_quotes) { cpos = i; break; }
        }
        if (cpos != std::string::npos) {
          this_comment = line.substr(cpos + 1);
          // strip '&' from comment so it isn't treated as continuation marker
          this_comment.erase(std::remove(this_comment.begin(), this_comment.end(), '&'),
                             this_comment.end());
          RemoveLeadingWhitespace(this_comment);
          RemoveTrailingWhitespace(this_comment);
          line = line.substr(0, cpos);
        }
      }
      auto last = line.find_last_not_of(' ');
      if (last == std::string::npos || last < first) {
        if (brace_depth > 0) brace_buf += "\n";
        continue;
      }
      std::string body = line.substr(first, last - first + 1);

      // If we are currently accumulating a brace-balanced block, keep doing so.
      if (brace_depth > 0) {
        brace_buf += "\n" + body;
        brace_depth += CountBraceDepth(body);
        if (brace_depth <= 0) {
          DeckLine dl{brace_start_loc, brace_buf, brace_comment};
          ir.blocks[current_block_].lines.push_back(std::move(dl));
          brace_buf.clear(); brace_comment.clear(); brace_depth = 0;
        }
        continue;
      }

      // Handle multi-line `&` continuation
      bool has_amp = (body.back() == '&');
      if (has_amp) body.pop_back();
      if (in_continuation) {
        cont_buf += " " + body;
        if (!this_comment.empty()) {
          if (!cont_comment.empty()) cont_comment += " ";
          cont_comment += this_comment;
        }
        if (has_amp) continue;
        body = cont_buf;
        this_comment = cont_comment;
        cont_buf.clear(); cont_comment.clear(); in_continuation = false;
        // recompute first/last
        first = body.find_first_not_of(' ');
        if (first == std::string::npos) continue;
      } else if (has_amp) {
        cont_buf = body;
        cont_comment = this_comment;
        cont_start_loc = line_num;
        in_continuation = true;
        continue;
      }

      // include "path"
      if (body.compare(0, 7, "include") == 0 &&
          (body.size() == 7 || body[7] == ' ' || body[7] == '"')) {
        size_t qopen = body.find('"');
        if (qopen == std::string::npos) {
          std::stringstream m;
          m << "Malformed include at line " << line_num;
          fatal(m);
        }
        size_t qclose = body.find('"', qopen + 1);
        if (qclose == std::string::npos) {
          std::stringstream m;
          m << "Malformed include at line " << line_num;
          fatal(m);
        }
        std::string inc_path = body.substr(qopen + 1, qclose - qopen - 1);
        std::filesystem::path resolved(inc_path);
        if (resolved.is_relative() && !base_dir.empty())
          resolved = std::filesystem::path(base_dir) / resolved;
        std::error_code ec;
        auto canonical = std::filesystem::canonical(resolved, ec);
        if (ec) {
          std::stringstream m;
          m << "Cannot resolve include '" << inc_path << "' at line " << line_num;
          fatal(m);
        }
        std::string canon = canonical.string();
        if (include_stack.count(canon)) {
          std::stringstream m;
          m << "Circular include '" << inc_path << "' at line " << line_num;
          fatal(m);
        }
        std::ifstream inc(canon);
        if (!inc.is_open()) {
          std::stringstream m;
          m << "Cannot open include '" << inc_path << "' at line " << line_num;
          fatal(m);
        }
        include_stack.insert(canon);
        Parse(inc, canonical.parent_path().string(), include_stack, ir);
        include_stack.erase(canon);
        continue;
      }

      // Suit header  <seg[(alias)]> or  <seg[(alias)]/seg[(alias)]/...>
      if (body.front() == '<') {
        auto close_angle = body.find('>');
        if (close_angle == std::string::npos) {
          std::stringstream m;
          m << "Missing '>' in suit header at line " << line_num;
          fatal(m);
        }
        std::string header = body.substr(1, close_angle - 1);
        RemoveWhitespace(header);
        if (header.empty()) {
          std::stringstream m;
          m << "Empty suit header at line " << line_num;
          fatal(m);
        }
        // Relative suit syntax — none of these update prev_suit_ so that
        // sibling blocks can all anchor to the same base.
        //
        //   <./sub>        child of the current anchor  (prev_suit_/sub)
        //   <../sub>       sibling one level up          (parent of prev_suit_)/sub
        //   <../../sub>    sibling two levels up, etc.
        bool is_relative_header = (!header.empty() && header[0] == '.');
        if (is_relative_header) {
          if (prev_suit_.empty()) {
            std::stringstream m;
            m << "Relative suit header with no prior suit at line " << line_num;
            fatal(m);
          }
          if (header.size() >= 2 && header[1] == '/') {
            // "./" → child of current anchor: prev_suit_ + "/rest"
            header = prev_suit_ + header.substr(1);
          } else {
            // Count leading "../" sequences to determine how many levels to go up.
            int levels = 0;
            size_t pos = 0;
            while (pos + 2 < header.size() &&
                   header[pos] == '.' && header[pos + 1] == '.' &&
                   header[pos + 2] == '/') {
              ++levels;
              pos += 3;
            }
            if (levels == 0) {
              std::stringstream m;
              m << "Malformed relative suit header '" << header
                << "' at line " << line_num;
              fatal(m);
            }
            // Strip `levels` trailing segments from prev_suit_.
            std::string base = prev_suit_;
            for (int i = 0; i < levels; ++i) {
              auto slash = base.rfind('/');
              if (slash == std::string::npos) {
                if (base.empty()) {
                  std::stringstream m;
                  m << "Relative suit header goes above root at line " << line_num;
                  fatal(m);
                }
                base.clear();
                break;
              }
              base = base.substr(0, slash);
            }
            std::string rest = header.substr(pos);
            header = base.empty() ? rest : (base + "/" + rest);
          }
        }
        // Split header into '/'-separated segments and parse each for an
        // optional `(alias)` suffix.
        std::vector<std::string> node_names;
        std::vector<std::string> eff_names;
        {
          size_t pos = 0;
          while (pos <= header.size()) {
            size_t slash = header.find('/', pos);
            std::string seg = header.substr(
                pos, slash == std::string::npos ? std::string::npos : slash - pos);
            std::string node = seg;
            std::string alias;
            auto popen = seg.find('(');
            if (popen != std::string::npos) {
              auto pclose = seg.find(')', popen);
              if (pclose == std::string::npos) {
                std::stringstream m;
                m << "Missing ')' in suit segment '" << seg
                  << "' at line " << line_num;
                fatal(m);
              }
              alias = seg.substr(popen + 1, pclose - popen - 1);
              node = seg.substr(0, popen);
            }
            if (node.empty()) {
              std::stringstream m;
              m << "Empty suit segment at line " << line_num;
              fatal(m);
            }
            node_names.push_back(node);
            eff_names.push_back(alias.empty() ? node : alias);
            if (slash == std::string::npos) break;
            pos = slash + 1;
          }
        }
        std::string effective_path;
        for (size_t i = 0; i < eff_names.size(); ++i) {
          if (i) effective_path += "/";
          effective_path += eff_names[i];
        }
        if (!is_relative_header) prev_suit_ = effective_path;
        BlockHeader bh;
        bh.suit_path = effective_path;
        bh.instance_name = eff_names.back(); // last-segment effective name
        bh.node_names = std::move(node_names);
        bh.loc = line_num;
        ir.blocks.push_back(Block{bh, {}, BlockMode::Declarative});
        current_block_ = ir.blocks.size() - 1;
        continue;
      }

      // Otherwise it's a regular line — check for brace opening
      int delta = CountBraceDepth(body);
      if (delta > 0) {
        brace_buf = body;
        brace_comment = this_comment;
        brace_start_loc = line_num;
        brace_depth = delta;
        continue;
      }
      // `__globals__` is a legacy directive (smoke-print of all globals).
      // The new pipeline does not implement it — silently drop the line so
      // it does not poison the surrounding block's classification.
      {
        std::string trimmed = body;
        Rummy::RemoveLeadingWhitespace(trimmed);
        Rummy::RemoveTrailingWhitespace(trimmed);
        if (trimmed == "__globals__") continue;
      }
      DeckLine dl{(in_continuation ? cont_start_loc : line_num), body, this_comment};
      ir.blocks[current_block_].lines.push_back(std::move(dl));
    }

    if (brace_depth > 0) {
      std::stringstream m;
      m << "Unterminated brace block starting at line " << brace_start_loc;
      fatal(m);
    }
    if (in_continuation) {
      std::stringstream m;
      m << "Unterminated '&' continuation starting at line " << cont_start_loc;
      fatal(m);
    }
  }

 private:
  size_t current_block_ = 0;
  std::string prev_suit_;
};

// --------------------------------------------------------------------------
// Line shape analysis & classification
// --------------------------------------------------------------------------

// Find the first top-level '=' in a line (quote-aware, not '==').
// Returns std::string::npos if none.
size_t FindAssign(const std::string &s) {
  bool in_quotes = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '"') in_quotes = !in_quotes;
    else if (!in_quotes && s[i] == '=') {
      if (i + 1 < s.size() && s[i + 1] == '=') { ++i; continue; }
      if (i > 0 && (s[i - 1] == '!' || s[i - 1] == '<' || s[i - 1] == '>' ||
                    s[i - 1] == '=')) continue;
      return i;
    }
  }
  return std::string::npos;
}

bool StartsWithPipsKeyword(const std::string &s) {
  static const std::set<std::string> kw = {"for", "while", "if",     "fn",
                                           "class", "return", "var"};
  size_t i = 0;
  while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
  return kw.count(s.substr(0, i)) > 0;
}

// Decompose an LHS name into (base_name, index_kind, raw_indices).
// index_kind: 0 = scalar  'a'
//             1 = single index 'a[3]'
//             2 = slice 'a[:3]' or 'a[1:4]'
// Also fatal-checks malformed brackets.
struct LhsInfo {
  std::string base;
  int kind = 0;
  std::string brackets; // raw substring from '[' to ']'
};

LhsInfo AnalyseLhs(const std::string &lhs, int loc) {
  LhsInfo li;
  auto ob = lhs.find('[');
  if (ob == std::string::npos) {
    li.base = lhs;
    return li;
  }
  auto cb = lhs.find(']', ob);
  if (cb == std::string::npos) {
    std::stringstream m;
    m << "Missing ']' in LHS '" << lhs << "' at line " << loc;
    fatal(m);
  }
  li.base = lhs.substr(0, ob);
  li.brackets = lhs.substr(ob, cb - ob + 1);
  li.kind = (li.brackets.find(':') != std::string::npos) ? 2 : 1;
  return li;
}

// Determine if an RHS expression is a vector LITERAL — either a bracketed
// list `[a, b, c]` or a comma-separated enumeration `a, b, c` at top level.
// Index/slice expressions like `n[0]` or `n[:3]` are NOT vector literals.
bool RhsIsVector(const std::string &rhs) {
  std::string s = rhs;
  RemoveLeadingWhitespace(s); RemoveTrailingWhitespace(s);
  if (!s.empty() && s.front() == '[') return true;
  bool in_quotes = false;
  int pd = 0, bd = 0;
  for (char c : s) {
    if (c == '"') in_quotes = !in_quotes;
    else if (!in_quotes && c == '(') ++pd;
    else if (!in_quotes && c == ')') --pd;
    else if (!in_quotes && c == '[') ++bd;
    else if (!in_quotes && c == ']') --bd;
    else if (!in_quotes && pd == 0 && bd == 0 && c == ',') return true;
  }
  return false;
}

// Split a bracketed list expression "[a, b, c]" or "a, b, c" into elements.
std::vector<std::string> SplitVectorRhs(const std::string &rhs_in, int loc) {
  std::string rhs = rhs_in;
  RemoveLeadingWhitespace(rhs); RemoveTrailingWhitespace(rhs);
  if (!rhs.empty() && rhs.front() == '[') {
    auto cb = rhs.find_last_of(']');
    if (cb == std::string::npos) {
      std::stringstream m;
      m << "Missing ']' in vector literal at line " << loc;
      fatal(m);
    }
    rhs = rhs.substr(1, cb - 1);
  }
  std::vector<std::string> out;
  std::string cur;
  bool in_quotes = false;
  int pd = 0;
  for (char c : rhs) {
    if (c == '"') { in_quotes = !in_quotes; cur += c; }
    else if (!in_quotes && c == '(') { ++pd; cur += c; }
    else if (!in_quotes && c == ')') { --pd; cur += c; }
    else if (!in_quotes && pd == 0 && c == ',') {
      RemoveLeadingWhitespace(cur); RemoveTrailingWhitespace(cur);
      out.push_back(cur); cur.clear();
    } else cur += c;
  }
  RemoveLeadingWhitespace(cur); RemoveTrailingWhitespace(cur);
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// Whole-word search for an identifier in an expression (quote-aware).
bool ExprMentions(const std::string &expr, const std::string &ident) {
  bool in_quotes = false;
  size_t i = 0;
  while (i < expr.size()) {
    char c = expr[i];
    if (c == '"') { in_quotes = !in_quotes; ++i; continue; }
    if (!in_quotes && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
      size_t j = i + 1;
      while (j < expr.size() &&
             (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_'))
        ++j;
      std::string tok = expr.substr(i, j - i);
      // Reject dotted access on either side
      bool dot_before = (i > 0 && expr[i - 1] == '.');
      bool dot_after = (j < expr.size() && expr[j] == '.');
      if (tok == ident && !dot_before && !dot_after) return true;
      i = j;
    } else ++i;
  }
  return false;
}

// Classify a single block.  Globals block always uses Pips emission style
// (we always emit line-by-line), but per-line is treated declaratively where
// possible.  In Strict mode, blocks with same-block self-references are kept
// in Declarative mode (the emission path translates the bare refs to
// `lvar.field`); in Loose mode they are demoted to Pips.
void ClassifyBlock(Block &b, FullDeck::Mode mode) {
  if (b.header.suit_path.empty()) {
    // Globals — we always go line-by-line; mode field unused
    b.mode = BlockMode::Pips;
    return;
  }
  std::set<std::string> lhs_bases;
  for (const auto &dl : b.lines) {
    const std::string &raw = dl.raw;
    if (StartsWithPipsKeyword(raw)) { b.mode = BlockMode::Pips; return; }
    auto eq = FindAssign(raw);
    if (eq == std::string::npos) { b.mode = BlockMode::Pips; return; }
    std::string lhs = raw.substr(0, eq);
    RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
    if (lhs.find('.') != std::string::npos) { b.mode = BlockMode::Pips; return; }
    LhsInfo li = AnalyseLhs(lhs, dl.loc);
    // No duplicate scalar LHS allowed in declarative mode
    if (li.kind == 0 && lhs_bases.count(li.base)) {
      b.mode = BlockMode::Pips; return;
    }
    lhs_bases.insert(li.base);
  }
  // Self-reference detection: in Loose mode, demote to Pips.  In Strict mode
  // keep Declarative — the emission path will rewrite bare same-block-LHS
  // refs to `lvar.field`.
  if (mode == FullDeck::Mode::Loose) {
    for (const auto &dl : b.lines) {
      const std::string &raw = dl.raw;
      auto eq = FindAssign(raw);
      std::string rhs = raw.substr(eq + 1);
      for (const auto &base : lhs_bases) {
        if (ExprMentions(rhs, base)) { b.mode = BlockMode::Pips; return; }
      }
    }
  }
  b.mode = BlockMode::Declarative;
}

// --------------------------------------------------------------------------
// Token-level external suit translation.
// Translates external dotted references like "alpha.p" or "alpha.sub.f" into
// the VM object path "<vm_alpha>.<vm_sub>.f".  Bare identifiers are NOT
// rewritten.
// --------------------------------------------------------------------------
std::string TranslateExpr(const std::string &expr,
                          const std::set<std::string> &suit_paths,
                          const std::map<std::string, std::string> &suit_to_vm_name) {
  auto suit_object_path = [&](const std::string &suit) {
    auto parts = SplitSuitPath(suit);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i) out += ".";
      if (i == 0) {
        auto it = suit_to_vm_name.find(parts[0]);
        out += (it != suit_to_vm_name.end()) ? it->second : parts[0];
      } else {
        out += parts[i];
      }
    }
    return out;
  };

  std::string translated;
  bool in_quotes = false;
  size_t i = 0;
  while (i < expr.size()) {
    char c = expr[i];
    if (c == '"') { in_quotes = !in_quotes; translated += c; ++i; continue; }
    if (!in_quotes && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
      size_t j = i + 1;
      while (j < expr.size()) {
        char tc = expr[j];
        if (std::isalnum(static_cast<unsigned char>(tc)) || tc == '_' || tc == '.' ||
            tc == '[' || tc == ']' || tc == ':')
          ++j;
        else break;
      }
      std::string tok = expr.substr(i, j - i);
      std::string best_suit;
      size_t dot = tok.find('.');
      while (dot != std::string::npos) {
        std::string prefix = tok.substr(0, dot);
        std::string cand = prefix;
        std::replace(cand.begin(), cand.end(), '.', '/');
        if (suit_paths.count(cand)) best_suit = cand;
        dot = tok.find('.', dot + 1);
      }
      std::string replacement = tok;
      if (!best_suit.empty()) {
        const std::string ext = ExternalSuitDotPath(best_suit);
        replacement = suit_object_path(best_suit) + tok.substr(ext.size());
      }
      translated += replacement;
      i = j;
    } else { translated += c; ++i; }
  }
  return translated;
}

} // namespace

// ============================================================================
// Deck2 — Build pipeline
// ============================================================================

void FullDeck::Build(std::string fname, std::string prepends) {
  std::stringstream pss; pss << prepends; Build(pss);
  std::ifstream input(fname);
  if (!input.is_open()) {
    std::stringstream m; m << "Could not open file '" << fname << "'";
    fatal(m);
  }
  std::string base_dir = std::filesystem::path(fname).parent_path().string();
  std::stringstream ss; ss << input.rdbuf();
  BuildInternal(ss, base_dir);
  SaveGraph(std::cout);
}

void FullDeck::Build(std::istream &ss, std::string prepends) {
  std::stringstream pss; pss << prepends; Build(pss); Build(ss);
}
void FullDeck::Build(std::istream &ss, std::istream &prepends) { Build(prepends); Build(ss); }
void FullDeck::Build(std::istream &ss) { BuildInternal(ss, ""); }

// ---------------------------------------------------------------------------
// BuildInternal — parse, classify, emit ONE pips program, interpret, readback
// ---------------------------------------------------------------------------
void FullDeck::BuildInternal(std::istream &ss, const std::string &base_dir) {
  // Prepend the ctor-provided class definitions on every Build so each IR
  // sees them as ordinary globals.  This makes Strict-mode validation and
  // Loose-mode `user_declared_classes` tracking work uniformly across
  // incremental builds.
  std::stringstream combined;
  if (!class_defs_.empty()) combined << class_defs_ << "\n";
  combined << ss.rdbuf();

  std::string combined_str = combined.str();
  // 1) PARSE
  DeckIR ir;
  std::set<std::string> include_stack;
  IrParser parser;
  parser.Parse(combined, base_dir, include_stack, ir);
  ir.print();

  // 1b) Detect user-declared classes (any `class X { ... }` appearing in
  //     any block).  Auto-generation skips these so the user's definition
  //     is the single source of truth.  We also parse the field list of
  //     each class so Strict mode can validate that aliased child instances
  //     correspond to a real parent-class field.
  std::set<std::string> user_declared_classes;
  std::map<std::string, std::set<std::string>> user_class_fields;
  for (const auto &blk : ir.blocks) {
    std::string in_class; // non-empty while inside a class body
    auto process_line = [&](const std::string &raw) {
      size_t i = raw.find_first_not_of(' ');
      if (i == std::string::npos) return;
      if (in_class.empty()) {
        if (raw.compare(i, 6, "class ") != 0) return;
        size_t j = i + 6;
        while (j < raw.size() && raw[j] == ' ') ++j;
        size_t k = j;
        while (k < raw.size() &&
               (std::isalnum(static_cast<unsigned char>(raw[k])) || raw[k] == '_'))
          ++k;
        if (k > j) {
          std::string cname = raw.substr(j, k - j);
          user_declared_classes.insert(cname);
          in_class = cname;
          if (raw.find('}', k) != std::string::npos) in_class.clear();
        }
      } else {
        if (raw.compare(i, 4, "var ") == 0) {
          size_t j = i + 4;
          while (j < raw.size() && raw[j] == ' ') ++j;
          size_t k = j;
          while (k < raw.size() &&
                 (std::isalnum(static_cast<unsigned char>(raw[k])) || raw[k] == '_'))
            ++k;
          if (k > j) user_class_fields[in_class].insert(raw.substr(j, k - j));
        }
        if (raw.find('}') != std::string::npos) in_class.clear();
      }
    };
    for (const auto &dl : blk.lines) {
      // A single deck line may carry a multi-line class body in dl.raw, so
      // split on newlines before scanning.
      const std::string &raw = dl.raw;
      size_t pos = 0;
      while (pos <= raw.size()) {
        size_t nl = raw.find('\n', pos);
        std::string ln = raw.substr(pos, nl == std::string::npos
                                            ? std::string::npos
                                            : nl - pos);
        process_line(ln);
        if (nl == std::string::npos) break;
        pos = nl + 1;
      }
    }
  }

  // 1c) For every suit path, record the class name backing each segment.
  //     The class name is Capitalize(node_name) — derived from the original
  //     (un-aliased) segment name.  For multi-segment paths we also need
  //     the per-segment class for ancestors that may appear only as
  //     children (e.g. `<a/b>` registers class for "a" too).
  // Track aliased suits: `<parent/node(alias)>` means the instance is named
  // `alias` but the parent class declares a field named `node`.  In that
  // case we must create the child via setattr (even in Strict mode) because
  // the parent class doesn't declare `alias` as a field, while still
  // validating that `node` IS a field of the parent.
  std::map<std::string, std::string> suit_parent_field_; // aliased suit -> parent field name
  for (const auto &blk : ir.blocks) {
    if (blk.header.suit_path.empty()) continue;
    const auto &eff_parts = SplitSuitPath(blk.header.suit_path);
    const auto &nodes = blk.header.node_names;
    if (eff_parts.size() != nodes.size()) continue; // shouldn't happen
    std::string acc;
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (!acc.empty()) acc += "/";
      acc += eff_parts[i];
      // Insert-if-absent: when `..` substitution rewrites a header using
      // already-aliased segment names, those segments arrive as their own
      // node names (no alias), which would Capitalize incorrectly.  The
      // FIRST block that declares a segment is the authoritative source
      // for its class name.
      suit_class_name_.emplace(acc, Capitalize(nodes[i]));
      if (eff_parts[i] != nodes[i])
        suit_parent_field_.emplace(acc, nodes[i]);
    }
  }

  // 1d) Track top-level globals in source order.  Readback stores that order
  // in card_map["/"], which is the existing ordered container for globals.
  std::vector<std::string> ordered_globals;
  {
    std::set<std::string> seen;
    auto add_global = [&](const std::string &name) {
      if (name.empty() || name[0] == '_') return;
      if (seen.insert(name).second) ordered_globals.push_back(name);
    };
    for (const auto &blk : ir.blocks) {
      if (!blk.header.suit_path.empty()) continue;
      bool in_class = false;
      for (const auto &dl : blk.lines) {
        const std::string &raw = dl.raw;
        size_t pos = 0;
        while (pos <= raw.size()) {
          size_t nl = raw.find('\n', pos);
          std::string ln = raw.substr(
              pos, nl == std::string::npos ? std::string::npos : nl - pos);
          size_t i = ln.find_first_not_of(" \t");
          if (i == std::string::npos) {
            if (nl == std::string::npos) break;
            pos = nl + 1;
            continue;
          }
          if (in_class) {
            if (ln.find('}') != std::string::npos) in_class = false;
          } else if (ln.compare(i, 6, "class ") == 0) {
            if (ln.find('}', i + 6) == std::string::npos) in_class = true;
          } else {
            auto eq = FindAssign(ln);
            if (eq != std::string::npos) {
              std::string lhs = ln.substr(0, eq);
              size_t j = lhs.find_first_not_of(" \t");
              if (j != std::string::npos) lhs.erase(0, j);
              if (lhs.compare(0, 4, "var ") == 0) lhs.erase(0, 4);
              size_t k = lhs.find_first_not_of(" \t");
              if (k != std::string::npos) lhs.erase(0, k);
              size_t e = 0;
              while (e < lhs.size() &&
                     (std::isalnum(static_cast<unsigned char>(lhs[e])) ||
                      lhs[e] == '_'))
                ++e;
              if (e > 0) add_global(lhs.substr(0, e));
            }
          }
          if (nl == std::string::npos) break;
          pos = nl + 1;
        }
      }
    }
  }

  // 2) Register every suit path encountered & assign VM variable names
  //    (top-level suits only — child suits live as fields of their parent).
  for (auto &blk : ir.blocks) {
    if (blk.header.suit_path.empty()) continue;
    const auto &sp = blk.header.suit_path;
    if (deck.find(sp) == deck.end()) {
      deck[sp] = {};
      suits.push_back(sp);
      card_map[sp] = {};
    }
    auto parts = SplitSuitPath(sp);
    if (parts.size() == 1) {
      // top-level: variable name == the effective suit name.
      suit_to_vm_name_[sp] = parts[0];
    }
  }
  // Promote all top-level suits to suit_to_vm_name_ if missing (e.g. globals
  // mentioning only a child suit via dotted include).
  for (const auto &sp : suits) {
    if (sp == "/" || sp.empty()) continue;
    auto parts = SplitSuitPath(sp);
    if (parts.size() == 1 && !suit_to_vm_name_.count(sp)) {
      suit_to_vm_name_[sp] = parts[0];
    }
  }

  // 3) CLASSIFY
  for (auto &blk : ir.blocks) ClassifyBlock(blk, mode_);

  // 4) SCHEMA — walk IR and collect fields per suit (scalar field names only;
  //    vector fields like v[0] are populated via setattr, not via class decl).
  //    Also seed scalar fields from existing deck cards for incremental builds.
  std::map<std::string, std::vector<std::string>> class_fields; // suit -> ordered fields
  std::map<std::string, std::set<std::string>> class_fields_set;
  auto add_field = [&](const std::string &suit, const std::string &field) {
    if (field.empty()) return;
    if (field.find('[') != std::string::npos) return; // vector slot, skip
    auto &set = class_fields_set[suit];
    if (set.insert(field).second) class_fields[suit].push_back(field);
  };
  // Existing cards seed the schema
  for (const auto &[suit, cards] : deck) {
    if (suit == "/") continue;
    for (const auto &[name, _] : cards) add_field(suit, name);
  }
  // New IR fields
  for (const auto &blk : ir.blocks) {
    if (blk.header.suit_path.empty()) continue;
    if (blk.mode != BlockMode::Declarative) continue;
    for (const auto &dl : blk.lines) {
      auto eq = FindAssign(dl.raw);
      if (eq == std::string::npos) continue;
      std::string lhs = dl.raw.substr(0, eq);
      RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
      if (lhs.find('.') != std::string::npos) continue;
      LhsInfo li = AnalyseLhs(lhs, dl.loc);
      if (li.kind == 0) add_field(blk.header.suit_path, li.base);
    }
  }
  // Parent-child links: track child segments per parent suit so the
  // readback knows which fields are nested instances vs scalar cards.
  // We DO NOT auto-add the child segment as a class field — classes are
  // emitted empty and children are attached at runtime via setattr.
  std::map<std::string, std::vector<std::string>> child_segments;
  std::map<std::string, std::set<std::string>> child_segments_set;
  for (const auto &sp : suits) {
    if (sp == "/" || sp.empty()) continue;
    auto parts = SplitSuitPath(sp);
    if (parts.size() < 2) continue;
    std::string parent;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      if (!parent.empty()) parent += "/";
      parent += parts[i];
    }
    const std::string &child_seg = parts.back();
    auto &set = child_segments_set[parent];
    if (set.insert(child_seg).second) child_segments[parent].push_back(child_seg);
  }

  // Build set of known suit paths (for external-reference translation)
  std::set<std::string> known_suits;
  for (const auto &sp : suits)
    if (sp != "/" && !sp.empty()) known_suits.insert(sp);

  // 5) EMIT — one big program buffer
  vm = pips::VM(); // fresh VM every Build (we re-seed below)
  instance_to_suit_.clear();

  // Per-field comment map populated during lowering, consumed at readback.
  std::map<std::pair<std::string, std::string>, std::string> field_comments;

  std::stringstream prog;

  // 5a) class declarations.  Per the deck spec, every node's class is
  //     `Capitalize(node_name)` with an EMPTY body — fields are attached
  //     at runtime via setattr.  We emit one class per unique class name,
  //     skipping any name the user already declared.
  //
  //     In Strict mode we do NOT auto-generate empty class stubs; the user
  //     supplies the complete schema via ctor `class_defs` / globals
  //     `class X { var a; ... }` declarations.  Any class that gets
  //     instantiated but was not declared is a fatal Rummy error.
  auto class_name_for_suit = [&](const std::string &sp) {
    auto it = suit_class_name_.find(sp);
    return it != suit_class_name_.end() ? it->second : SuitClassName(sp);
  };
  if (mode_ == Mode::Strict) {
    // Validate every instantiated class is user-declared.
    for (const auto &[sp, cn] : suit_class_name_) {
      if (!user_declared_classes.count(cn)) {
        std::stringstream m;
        m << "Strict mode: suit '" << sp << "' uses class '" << cn
          << "' which has not been declared.  Add a `class " << cn
          << " { ... }` definition to the prepended class_defs or globals.";
        fatal(m);
      }
    }
    // Validate aliased child suits: the parent class must declare the
    // node name as a field (the alias itself is dynamically attached via
    // setattr, but the underlying schema field must exist).
    for (const auto &[sp, field] : suit_parent_field_) {
      auto parts = SplitSuitPath(sp);
      if (parts.size() < 2) continue;
      std::string parent;
      for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (i) parent += "/";
        parent += parts[i];
      }
      auto pit = suit_class_name_.find(parent);
      if (pit == suit_class_name_.end()) continue;
      const std::string &pcls = pit->second;
      auto fit = user_class_fields.find(pcls);
      if (fit == user_class_fields.end() || !fit->second.count(field)) {
        std::stringstream m;
        m << "Strict mode: aliased suit '" << sp << "' implies field '"
          << field << "' on class '" << pcls
          << "', but no such field is declared.";
        fatal(m);
      }
    }
  } else {
    std::set<std::string> emitted_classes;
    std::vector<std::string> ordered_classes;
    for (const auto &[sp, cn] : suit_class_name_) {
      (void)sp;
      if (emitted_classes.insert(cn).second) ordered_classes.push_back(cn);
    }
    std::sort(ordered_classes.begin(), ordered_classes.end());
    for (const auto &cn : ordered_classes) {
      if (user_declared_classes.count(cn)) continue;
      prog << "class " << cn << " {}\n";
    }
  }

  // Hoist user-declared class definitions from the globals block to the top
  // of the program so that the per-suit `var x = new X {}` instantiations
  // emitted in step 5b can reference them.  We record the source locations
  // of the hoisted lines so step 5c can skip re-emitting them.
  std::set<int> hoisted_global_locs;
  for (const auto &blk : ir.blocks) {
    if (!blk.header.suit_path.empty()) continue;
    for (const auto &dl : blk.lines) {
      const std::string &raw = dl.raw;
      size_t i = raw.find_first_not_of(' ');
      if (i == std::string::npos) continue;
      if (raw.compare(i, 6, "class ") != 0) continue;
      prog << raw << "\n";
      hoisted_global_locs.insert(dl.loc);
    }
  }

  // 5b) Seed existing cards: re-create instances for any suit already present
  //     in the deck (incremental builds), and assign scalar field values.
  auto translate = [&](const std::string &e) {
    return TranslateExpr(e, known_suits, suit_to_vm_name_);
  };
  // Build bare-identifier lookup: in declarative mode, an unqualified
  // identifier on the RHS that matches a scalar field of an earlier-declared
  // suit is resolved to "<vmname>.<field>" (legacy bare-reference behaviour).
  // Later declarations overwrite earlier ones, mirroring deck.cpp's card_map.
  std::map<std::string, std::string> bare_to_qualified;
  for (const auto &sp : suits) {
    if (sp == "/" || sp.empty()) continue;
    auto parts = SplitSuitPath(sp);
    // Build the dotted VM path for this suit, parents first.
    std::string vm_path;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i) vm_path += ".";
      if (i == 0) {
        auto it = suit_to_vm_name_.find(parts[0]);
        vm_path += (it != suit_to_vm_name_.end()) ? it->second : parts[0];
      } else {
        vm_path += parts[i];
      }
    }
    auto fit = class_fields.find(sp);
    if (fit == class_fields.end()) continue;
    for (const auto &fname : fit->second) {
      // Skip names that are themselves child-segment links.
      auto cit = child_segments_set.find(sp);
      if (cit != child_segments_set.end() && cit->second.count(fname)) continue;
      bare_to_qualified[fname] = vm_path + "." + fname;
    }
  }
  // In declarative mode we also want bare identifiers to be rewritten.
  auto translate_decl = [&](const std::string &e,
                            const std::set<std::string> &same_block_lhs) {
    std::string base = TranslateExpr(e, known_suits, suit_to_vm_name_);
    // Walk tokens; if a bare identifier (not preceded/followed by '.', not
    // followed by '(' or '[') matches a known field, rewrite it.
    std::string out;
    bool in_quotes = false;
    size_t i = 0;
    while (i < base.size()) {
      char c = base[i];
      if (c == '"') { in_quotes = !in_quotes; out += c; ++i; continue; }
      if (!in_quotes && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
        size_t j = i + 1;
        while (j < base.size() &&
               (std::isalnum(static_cast<unsigned char>(base[j])) || base[j] == '_'))
          ++j;
        std::string tok = base.substr(i, j - i);
        bool prev_dot = (i > 0 && base[i - 1] == '.');
        char next_c = (j < base.size()) ? base[j] : '\0';
        bool followed_by_member_or_call =
            (next_c == '.' || next_c == '(');
        if (!prev_dot && !followed_by_member_or_call &&
            !same_block_lhs.count(tok)) {
          auto it = bare_to_qualified.find(tok);
          if (it != bare_to_qualified.end()) tok = it->second;
        }
        out += tok;
        i = j;
      } else { out += c; ++i; }
    }
    return out;
  };
  // Card value -> pips literal
  std::function<std::string(const pips::Value &)> value_to_literal =
      [&](const pips::Value &v) -> std::string {
    std::ostringstream os;
    if (v.type == pips::ValueType::STRING) {
      os << "\"" << AS_STRING(v) << "\"";
    } else if (v.type == pips::ValueType::BOOL) {
      os << (v.as.boolean ? "true" : "false");
    } else if (v.type == pips::ValueType::NUMBER) {
      os << std::setprecision(std::numeric_limits<double>::max_digits10)
         << v.as.number;
    } else if (v.type == pips::ValueType::NIL) {
      os << "nil";
    } else if (v.type == pips::ValueType::VECTOR) {
      os << "[";
      auto *vo = AS_VECTOR(v);
      if (vo) {
        for (size_t i = 0; i < vo->elements.size(); ++i) {
          if (i) os << ", ";
          os << value_to_literal(vo->elements[i]);
        }
      }
      os << "]";
    } else {
      // unsupported (instance values are linked separately)
      os << "nil";
    }
    return os.str();
  };
  // Emit instance creations for all known suits in path-length order (parents first)
  std::vector<std::string> all_suits_sorted;
  for (const auto &sp : suits)
    if (sp != "/" && !sp.empty()) all_suits_sorted.push_back(sp);
  std::sort(all_suits_sorted.begin(), all_suits_sorted.end(),
            [](const std::string &a, const std::string &b) {
              auto la = SplitSuitPath(a).size();
              auto lb = SplitSuitPath(b).size();
              if (la != lb) return la < lb;
              return a < b;
            });

  // Per-suit variable reference: top-level suits live as globals named after
  // the effective top segment; nested suits are accessed via dotted path
  // from the top instance (`prob.mat1`).  No mangling or synthetic names.
  auto suit_local_var = [&](const std::string &sp) {
    auto parts = SplitSuitPath(sp);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i) out += ".";
      out += parts[i];
    }
    return out;
  };

  // Lazy instance emission: each suit is materialized on first reference,
  // walking its ancestor chain so that parents always exist before children.
  // Pre-existing deck cards (from prior incremental Build calls) are seeded
  // immediately after the `new` line so their values are in place before
  // any subsequent block-body setattrs.
  std::set<std::string> instantiated;
  std::function<void(const std::string &)> emit_instance =
      [&](const std::string &sp) {
        auto parts = SplitSuitPath(sp);
        std::string acc;
        for (size_t i = 0; i < parts.size(); ++i) {
          if (i) acc += "/";
          acc += parts[i];
          if (!instantiated.insert(acc).second) continue;
          const std::string lvar = suit_local_var(acc);
          if (i == 0) {
            prog << "var " << lvar << " = new "
                 << class_name_for_suit(acc) << " {}\n";
          } else {
            std::string parent;
            for (size_t j = 0; j < i; ++j) {
              if (j) parent += ".";
              parent += parts[j];
            }
            // Aliased child instances (e.g. `<parthenon/output(out1)>`)
            // attach the instance via setattr regardless of mode — the
            // parent class declares the underlying node name as a field,
            // not the alias, so a direct SET_PROPERTY would fail strict
            // checks.
            bool aliased = suit_parent_field_.count(acc) > 0;
            if (mode_ == Mode::Strict && !aliased) {
              prog << parent << "." << parts[i] << " = new "
                   << class_name_for_suit(acc) << " {}\n";
            } else {
              prog << "setattr(" << parent << ", \"" << parts[i]
                   << "\", new " << class_name_for_suit(acc) << " {})\n";
            }
          }
          // Seed any prior-build deck cards for this suit.
          auto dit = deck.find(acc);
          if (dit != deck.end()) {
            for (const auto &[name, card] : dit->second) {
              if (mode_ == Mode::Strict) {
                prog << lvar << "." << name << " = "
                     << value_to_literal(card.GetValue()) << "\n";
              } else {
                prog << "setattr(" << lvar << ", \"" << name << "\", "
                     << value_to_literal(card.GetValue()) << ")\n";
              }
            }
          }
        }
      };

  // 5c) Emit per-block lowering.
  //     Globals block (header.suit_path == "") emits raw / `var name = expr`.
  //     Suit blocks lazily materialize their suit (and ancestors) on first
  //     touch, then emit setattrs for the block body in source order.
  std::map<int, int> prog_line_to_loc; // 1-based prog line -> source loc
  auto emit_line = [&](const std::string &line_text, int src_loc) {
    int prog_line = 1;
    for (char c : prog.str()) if (c == '\n') ++prog_line;
    prog_line_to_loc[prog_line] = src_loc;
    prog << line_text << "\n";
  };

  for (const auto &blk : ir.blocks) {
    if (blk.header.suit_path.empty()) {
      // GLOBALS block
      for (const auto &dl : blk.lines) {
        // Skip class declarations: they were already hoisted to the top of
        // the program above.  Match by raw text rather than source location
        // because loc collisions are possible across multiple input files
        // (e.g. an include file's line N and the main file's line N).
        {
          const std::string &r = dl.raw;
          size_t i = r.find_first_not_of(' ');
          if (i != std::string::npos && r.compare(i, 6, "class ") == 0)
            continue;
        }
        const std::string &raw = dl.raw;
        auto eq = FindAssign(raw);
        if (eq == std::string::npos || StartsWithPipsKeyword(raw)) {
          // raw pips statement
          emit_line(translate(raw), dl.loc);
          continue;
        }
        std::string lhs = raw.substr(0, eq);
        std::string rhs = raw.substr(eq + 1);
        RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
        RemoveLeadingWhitespace(rhs); RemoveTrailingWhitespace(rhs);
        // Dotted LHS (e.g. `suit.f = expr`) -> treat as cross-suit assignment
        if (lhs.find('.') != std::string::npos) {
          // translate LHS via suit translation too
          emit_line(translate(lhs) + " = " + translate(rhs), dl.loc);
          continue;
        }
        // Vector handling
        LhsInfo li = AnalyseLhs(lhs, dl.loc);
        if (li.kind == 0 && !RhsIsVector(rhs)) {
          emit_line("var " + lhs + " = " + translate(rhs), dl.loc);
        } else if (li.kind == 0 && RhsIsVector(rhs)) {
          // Vector RHS at globals scope: one `var v = [...]` statement.
          auto vals = SplitVectorRhs(rhs, dl.loc);
          std::string vec_lit = "[";
          for (size_t i = 0; i < vals.size(); ++i) {
            if (i) vec_lit += ", ";
            vec_lit += translate(vals[i]);
          }
          vec_lit += "]";
          emit_line("var " + li.base + " = " + vec_lit, dl.loc);
        } else if (li.kind == 1) {
          emit_line("var " + lhs + " = " + translate(rhs), dl.loc);
        } else {
          // slice
          std::vector<std::string> vals;
          if (RhsIsVector(rhs)) vals = SplitVectorRhs(rhs, dl.loc);
          auto names = SplitString(lhs, dl.loc, vals.empty() ? 0 : vals.size());
          if (vals.empty()) {
            // a[:N] = b[:N]
            auto rvals = SplitString(rhs, dl.loc, names.size());
            for (size_t i = 0; i < names.size(); ++i)
              emit_line("var " + names[i] + " = " + translate(rvals[i]), dl.loc);
          } else {
            if (names.size() > vals.size()) {
              std::stringstream m;
              m << "More slice targets than values at line " << dl.loc;
              fatal(m);
            }
            for (size_t i = 0; i < names.size(); ++i)
              emit_line("var " + names[i] + " = " + translate(vals[i]), dl.loc);
          }
        }
      }
      continue;
    }

    // SUIT block
    const std::string &sp = blk.header.suit_path;
    const std::string lvar = suit_local_var(sp);

    if (blk.mode == BlockMode::Declarative) {
      // Collect same-block LHS bases so bare references to them can be
      // detected (and, in Strict mode, rewritten to `lvar.field`).
      std::set<std::string> same_block_lhs;
      bool all_simple_scalar = true;
      bool has_self_ref = false;
      for (const auto &dl : blk.lines) {
        auto eq = FindAssign(dl.raw);
        if (eq == std::string::npos) { all_simple_scalar = false; continue; }
        std::string lhs = dl.raw.substr(0, eq);
        RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
        if (lhs.find('.') != std::string::npos) { all_simple_scalar = false; continue; }
        LhsInfo li = AnalyseLhs(lhs, dl.loc);
        if (li.kind != 0) all_simple_scalar = false;
        if (!li.base.empty()) same_block_lhs.insert(li.base);
        // Record any inline comment for later readback.
        if (!dl.comment.empty() && !li.base.empty())
          field_comments[{sp, li.base}] = dl.comment;
      }
      for (const auto &dl : blk.lines) {
        auto eq = FindAssign(dl.raw);
        if (eq == std::string::npos) continue;
        std::string rhs = dl.raw.substr(eq + 1);
        for (const auto &base : same_block_lhs) {
          if (ExprMentions(rhs, base)) { has_self_ref = true; break; }
        }
        if (has_self_ref) break;
      }
      auto dit_prior = deck.find(sp);
      bool has_prior_cards = (dit_prior != deck.end() && !dit_prior->second.empty());

      const bool use_initializer =
          (mode_ == Mode::Strict && all_simple_scalar && !has_self_ref
           && !has_prior_cards);

      // Translate an RHS expression.  In Strict mode with self-refs, bare
      // identifiers that match a same-block LHS base are rewritten to
      // `lvar.field` so they resolve against the current instance.  Loose
      // mode (and Strict mode without self-refs) keeps the legacy behaviour
      // where same-block-LHS bare refs are left literal.
      std::set<std::string> tr_skip = same_block_lhs;
      std::map<std::string, std::string> rewrite_self;
      if (mode_ == Mode::Strict && has_self_ref) {
        for (const auto &b : same_block_lhs) rewrite_self[b] = lvar + "." + b;
      }
      auto tr = [&](const std::string &e) {
        std::string out = translate_decl(e, tr_skip);
        if (rewrite_self.empty()) return out;
        // Token-level rewrite of bare identifiers in `rewrite_self`.
        std::string res;
        bool in_quotes = false;
        size_t i = 0;
        while (i < out.size()) {
          char c = out[i];
          if (c == '"') { in_quotes = !in_quotes; res += c; ++i; continue; }
          if (!in_quotes && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
            size_t j = i + 1;
            while (j < out.size() &&
                   (std::isalnum(static_cast<unsigned char>(out[j])) || out[j] == '_'))
              ++j;
            std::string tok = out.substr(i, j - i);
            bool prev_dot = (i > 0 && out[i - 1] == '.');
            char next_c = (j < out.size()) ? out[j] : '\0';
            bool followed_by_call = (next_c == '(');
            if (!prev_dot && !followed_by_call) {
              auto it = rewrite_self.find(tok);
              if (it != rewrite_self.end()) tok = it->second;
            }
            res += tok;
            i = j;
          } else { res += c; ++i; }
        }
        return res;
      };

      if (use_initializer) {
        // ---------------- Initializer-syntax emission ----------------
        // Materialize ancestors only (the leaf is created inline below).
        auto parts = SplitSuitPath(sp);
        std::string parent_path;
        if (parts.size() >= 2) {
          std::string ancestor;
          for (size_t i = 0; i + 1 < parts.size(); ++i) {
            if (i) ancestor += "/";
            ancestor += parts[i];
          }
          emit_instance(ancestor);
          for (size_t i = 0; i + 1 < parts.size(); ++i) {
            if (i) parent_path += ".";
            parent_path += parts[i];
          }
        }
        // Emit:  var <leaf> = new <Cls> {                       (top-level)
        //        <parent>.<leaf> = new <Cls> {                 (nested, plain)
        //        setattr(<parent>, "<leaf>", new <Cls> {       (nested, aliased)
        const std::string leaf = parts.back();
        const std::string cls = class_name_for_suit(sp);
        const bool aliased = suit_parent_field_.count(sp) > 0;
        std::string open;
        std::string close;
        if (parts.size() == 1) {
          open = "var " + leaf + " = new " + cls + " {";
          close = "}";
        } else if (aliased) {
          open = "setattr(" + parent_path + ", \"" + leaf + "\", new " + cls + " {";
          close = "})";
        } else {
          open = parent_path + "." + leaf + " = new " + cls + " {";
          close = "}";
        }
        emit_line(open, blk.header.loc);
        for (const auto &dl : blk.lines) {
          const std::string &raw = dl.raw;
          auto eq = FindAssign(raw);
          if (eq == std::string::npos) continue;
          std::string lhs = raw.substr(0, eq);
          std::string rhs = raw.substr(eq + 1);
          RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
          RemoveLeadingWhitespace(rhs); RemoveTrailingWhitespace(rhs);
          LhsInfo li = AnalyseLhs(lhs, dl.loc);
          std::string val;
          if (RhsIsVector(rhs)) {
            auto vals = SplitVectorRhs(rhs, dl.loc);
            val = "[";
            for (size_t i = 0; i < vals.size(); ++i) {
              if (i) val += ", ";
              val += tr(vals[i]);
            }
            val += "]";
          } else {
            val = tr(rhs);
          }
          emit_line("  ." + li.base + " = " + val + ";", dl.loc);
        }
        emit_line(close, blk.header.loc);
        instantiated.insert(sp);
      } else {
        // ---------------- Line-by-line emission ----------------
        emit_instance(sp);
        // Helper that emits either `setattr(lvar, "field", val)` (Loose) or
        // `lvar.field = val` (Strict).  In Strict mode, writes to undeclared
        // fields are caught by pips itself at compile time.
        auto emit_assign = [&](const std::string &field, const std::string &val,
                               int loc) {
          if (mode_ == Mode::Strict)
            emit_line(lvar + "." + field + " = " + val, loc);
          else
            emit_line("setattr(" + lvar + ", \"" + field + "\", " + val + ")",
                      loc);
        };
        // Emit one assignment per field/vector slot.  (We already created the
        // instance during the seeding pass.)
        for (const auto &dl : blk.lines) {
          const std::string &raw = dl.raw;
          auto eq = FindAssign(raw);
          std::string lhs = raw.substr(0, eq);
          std::string rhs = raw.substr(eq + 1);
          RemoveTrailingWhitespace(lhs); RemoveLeadingWhitespace(lhs);
          RemoveLeadingWhitespace(rhs); RemoveTrailingWhitespace(rhs);
          LhsInfo li = AnalyseLhs(lhs, dl.loc);
          if (li.kind == 0 && !RhsIsVector(rhs)) {
            emit_assign(li.base, tr(rhs), dl.loc);
          } else if (li.kind == 0 && RhsIsVector(rhs)) {
            auto vals = SplitVectorRhs(rhs, dl.loc);
            std::string vec_lit = "[";
            for (size_t i = 0; i < vals.size(); ++i) {
              if (i) vec_lit += ", ";
              vec_lit += tr(vals[i]);
            }
            vec_lit += "]";
            emit_assign(li.base, vec_lit, dl.loc);
          } else if (li.kind == 1) {
            emit_line("setattr(" + lvar + ", \"" + lhs + "\", " + tr(rhs) + ")",
                      dl.loc);
          } else {
            std::vector<std::string> vals;
            if (RhsIsVector(rhs)) vals = SplitVectorRhs(rhs, dl.loc);
            auto names = SplitString(lhs, dl.loc, vals.empty() ? 0 : vals.size());
            if (vals.empty()) {
              auto rvals = SplitString(rhs, dl.loc, names.size());
              for (size_t i = 0; i < names.size(); ++i)
                emit_line("setattr(" + lvar + ", \"" + names[i] + "\", "
                              + tr(rvals[i]) + ")",
                          dl.loc);
            } else {
              if (names.size() > vals.size()) {
                std::stringstream m;
                m << "More slice targets than values at line " << dl.loc;
                fatal(m);
              }
              for (size_t i = 0; i < names.size(); ++i)
                emit_line("setattr(" + lvar + ", \"" + names[i] + "\", "
                              + tr(vals[i]) + ")",
                          dl.loc);
            }
          }
        }
      }
    } else {
      // PIPS-mode block: emit each line verbatim with external-suit
      // translation.  Bare identifiers are NOT rewritten — the user qualifies
      // field access via the suit's instance name.
      emit_instance(sp);
      for (const auto &dl : blk.lines) {
        emit_line(translate(dl.raw), dl.loc);
      }
    }
  }

  // Materialize any remaining suits that exist in the deck (from prior
  // incremental Build calls) but have no block in this IR.  These must be
  // instantiated so subsequent reads can find them.
  for (const auto &sp : all_suits_sorted) emit_instance(sp);

  // 6) COMPILE
  // output the prog
  std::cout << "--- generated pips program ---\n" << prog.str() << "--- end program ---\n";
  const std::string program = prog.str();

  pips::VTable locals;
  if (vm.interpret(program.c_str(), '\n', locals) != pips::InterpretResult::OK) {
    std::stringstream m;
    m << "Failed to compile generated pips program.\n--- program ---\n"
      << program << "\n--- end program ---";
    fatal(m);
  }

  // 7) READBACK — walk vm.globals and rebuild deck table.
  //    First reset the cards we will repopulate (preserve "/" entries that
  //    came from prior Build calls only if the globals block didn't touch
  //    them).  Simpler: clear all suit contents and rebuild.
  for (auto &[suit, cards] : deck) cards.clear();
  for (auto &[suit, cm] : card_map) cm.clear();

  // Map suit -> instance pointer using the names we used in the program.
  // For top-level suits the variable lives in vm.globals.  For child suits we
  // descend through parent fields by name.
  auto resolve_instance = [&](const std::string &sp) -> pips::Instance * {
    auto parts = SplitSuitPath(sp);
    if (parts.empty()) return nullptr;
    auto it = suit_to_vm_name_.find(parts[0]);
    const std::string root =
        (it != suit_to_vm_name_.end()) ? it->second : parts[0];
    auto g = vm.globals.find(root);
    if (g == vm.globals.end() || g->second.type != pips::ValueType::INSTANCE)
      return nullptr;
    pips::Instance *cur = g->second.as.instance;
    for (size_t i = 1; i < parts.size(); ++i) {
      auto f = cur->fields.find(parts[i]);
      if (f == cur->fields.end() || f->second.type != pips::ValueType::INSTANCE)
        return nullptr;
      cur = f->second.as.instance;
    }
    return cur;
  };

  // Auto-register suits for any top-level INSTANCE global that wasn't
  // introduced by a `<Suit>` block.  This lets saved DeckGraph files
  // (which are plain pips creation statements with no `<>` headers)
  // round-trip through Build() and still expose their cards via the
  // suit-based API.  Nested INSTANCE fields are added as child suits.
  {
    std::function<void(const std::string &, pips::Instance *)> register_inst =
        [&](const std::string &sp, pips::Instance *inst) {
          if (!inst) return;
          if (deck.find(sp) == deck.end()) {
            deck[sp] = {};
            suits.push_back(sp);
            card_map[sp] = {};
          }
          for (const auto &[fname, fval] : inst->fields) {
            if (fval.type == pips::ValueType::INSTANCE) {
              register_inst(sp + "/" + fname, fval.as.instance);
            }
          }
        };
    for (const auto &[gname, gval] : vm.globals) {
      if (gval.type != pips::ValueType::INSTANCE) continue;
      if (gname.empty() || gname[0] == '_') continue;
      if (deck.find(gname) != deck.end()) continue; // already a known suit
      suit_to_vm_name_[gname] = gname;
      register_inst(gname, gval.as.instance);
    }
  }

  for (const auto &sp : suits) {
    if (sp == "/" || sp.empty()) continue;
    pips::Instance *inst = resolve_instance(sp);
    if (!inst) continue;
    instance_to_suit_[inst] = sp;
    auto parts = SplitSuitPath(sp);
    std::set<std::string> child_field_names;
    auto cit = child_segments_set.find(sp);
    if (cit != child_segments_set.end()) child_field_names = cit->second;
    // Build a snapshot of all fields on the instance.
    std::unordered_map<std::string, pips::Value> all_fields;
    for (const auto &[fname, fval] : inst->fields) all_fields[fname] = fval;

    // First, emit fields in declared/source order so card_map preserves
    // the order found in the deck file.
    auto handle_field = [&](const std::string &fname) {
      auto it = all_fields.find(fname);
      if (it == all_fields.end()) return;
      const pips::Value &fval = it->second;
      if (fval.type == pips::ValueType::NIL) return;
      if (fval.type == pips::ValueType::INSTANCE) return; // child link
      auto bracket = fname.find('[');
      std::string base = (bracket != std::string::npos) ? fname.substr(0, bracket) : fname;
      std::string comment;
      auto cmt = field_comments.find({sp, base});
      if (cmt != field_comments.end()) comment = cmt->second;
      // VECTOR field: expand into indexed Cards (base[0], base[1], ...)
      // so the legacy IsCardVector / GetVector contract still applies.
      if (fval.type == pips::ValueType::VECTOR) {
        auto *vo = AS_VECTOR(fval);
        if (!vo) return;
        auto &cm = card_map[sp];
        if (std::find(cm.begin(), cm.end(), base) == cm.end()) cm.push_back(base);
        for (size_t i = 0; i < vo->elements.size(); ++i) {
          std::string slot = base + "[" + std::to_string(i) + "]";
          Card c(sp, slot, vo->elements[i],
                 (i == 0 ? comment : std::string{}), -1);
          deck[sp][slot] = c;
        }
        return;
      }
      Card c(sp, fname, fval, comment, -1);
      deck[sp][fname] = c;
      auto &cm = card_map[sp];
      if (std::find(cm.begin(), cm.end(), base) == cm.end()) cm.push_back(base);
    };
    std::set<std::string> handled;
    auto fit = class_fields.find(sp);
    if (fit != class_fields.end()) {
      for (const auto &fname : fit->second) {
        handle_field(fname);
        handled.insert(fname);
      }
    }
    // Then sweep dynamic/setattr-created fields not in the declared schema.
    for (const auto &[fname, _] : all_fields) {
      if (handled.count(fname)) continue;
      handle_field(fname);
    }
  }

  // Globals (scalar + vector globals). First preserve parsed order via
  // card_map["/"], then sweep any dynamic leftovers that only exist in the VM.
  auto handle_global = [&](const std::string &gname, const pips::Value &gval) {
    if (gval.type == pips::ValueType::INSTANCE) return;
    if (gval.type == pips::ValueType::NIL) return;
    if (gname.rfind("__rummy_inst_", 0) == 0) return;
    auto &cm = card_map["/"];
    if (gval.type == pips::ValueType::VECTOR) {
      auto *vo = AS_VECTOR(gval);
      if (!vo) return;
      if (std::find(cm.begin(), cm.end(), gname) == cm.end()) cm.push_back(gname);
      for (size_t i = 0; i < vo->elements.size(); ++i) {
        std::string slot = gname + "[" + std::to_string(i) + "]";
        Card c("/", slot, vo->elements[i], "", -1);
        deck["/"][slot] = c;
      }
      return;
    }
    Card c("/", gname, gval, "", -1);
    deck["/"][gname] = c;
    if (std::find(cm.begin(), cm.end(), gname) == cm.end()) cm.push_back(gname);
  };
  std::set<std::string> handled_globals;
  for (const auto &gname : ordered_globals) {
    auto it = vm.globals.find(gname);
    if (it == vm.globals.end()) continue;
    handle_global(gname, it->second);
    handled_globals.insert(gname);
  }
  for (const auto &[gname, gval] : vm.globals) {
    if (handled_globals.count(gname)) continue;
    handle_global(gname, gval);
  }
}

// ---------------------------------------------------------------------------
// CopyVmState
// ---------------------------------------------------------------------------
void FullDeck::RebuildInstanceRegistry() {
  instance_to_suit_.clear();
  std::function<void(pips::Instance *, const std::string &)> scan =
      [&](pips::Instance *inst, const std::string &sp) {
        if (!inst || instance_to_suit_.count(inst)) return;
        instance_to_suit_[inst] = sp;
        for (const auto &[fn, fv] : inst->fields) {
          if (fv.type != pips::ValueType::INSTANCE) continue;
          // Try to interpret field name as a child suit segment
          // (best-effort; works for VmSuitSegment-mangled names)
          std::string child_sp = sp + "/" + fn;
          if (std::find(suits.begin(), suits.end(), child_sp) != suits.end()) {
            scan(fv.as.instance, child_sp);
          }
        }
      };
  for (const auto &[gn, gv] : vm.globals) {
    if (gv.type != pips::ValueType::INSTANCE) continue;
    for (const auto &sp : suits) {
      if (sp == "/" || sp.empty()) continue;
      auto parts = SplitSuitPath(sp);
      if (parts.size() != 1) continue;
      auto it = suit_to_vm_name_.find(sp);
      const std::string expected =
          (it != suit_to_vm_name_.end()) ? it->second : parts[0];
      if (gn == expected) { scan(gv.as.instance, sp); break; }
    }
  }
}

std::vector<Card> FullDeck::FindSuitInOrder(const std::string &suit, const bool fuzzy) const {
  std::vector<Card> sub;
  if (fuzzy) sub = FindSuitFuzzy(suit);
  else {
    auto it = deck.find(suit);
    if (it == deck.end()) {
      if (suit != "/") {
        std::stringstream m; m << "Suit '" << suit << "' not found in the deck.";
        fatal(m);
      }
    } else {
      for (const auto &[_, c] : it->second) sub.push_back(c);
    }
  }
  // Order by card_map order
  auto cm_it = card_map.find(suit);
  if (cm_it != card_map.end()) {
    std::map<std::string, int> base_order;
    for (size_t i = 0; i < cm_it->second.size(); ++i)
      base_order[cm_it->second[i]] = (int)i;
    std::sort(sub.begin(), sub.end(), [&](const Card &a, const Card &b) {
      auto base = [](const std::string &n) {
        auto p = n.find('[');
        return p == std::string::npos ? n : n.substr(0, p);
      };
      auto ai = base_order.find(base(a.name));
      auto bi = base_order.find(base(b.name));
      int ao = (ai != base_order.end()) ? ai->second : std::numeric_limits<int>::max();
      int bo = (bi != base_order.end()) ? bi->second : std::numeric_limits<int>::max();
      if (ao != bo) return ao < bo;
      return a.name < b.name;
    });
  } else {
    std::sort(sub.begin(), sub.end(),
              [](const Card &a, const Card &b) { return a.name < b.name; });
  }
  return sub;
}

// ---------------------------------------------------------------------------
// Device function packing & calling
// ---------------------------------------------------------------------------
bool FullDeck::PackDeviceFunction(const std::string &name, std::string &error) {
  // Discard any prior packing under the same name to keep storage tight.
  auto sit = device_modules_.find(name);
  if (sit != device_modules_.end()) {
    device_modules_.erase(sit);
    device_entry_ids_.erase(name);
    device_function_order_.erase(std::remove(device_function_order_.begin(),
                                             device_function_order_.end(),
                                             name),
                                 device_function_order_.end());
  }

  pips::device::DeviceModuleStorage storage;
  std::uint32_t entry_id = 0;
  if (!pips::device::pack_function(vm, name, storage, entry_id, error)) {
    return false;
  }
  device_modules_.emplace(name, std::move(storage));
  device_entry_ids_[name] = entry_id;
  device_function_order_.push_back(name);
  return true;
}

void FullDeck::PackDeviceFunction(const std::string &name) {
  std::string error;
  if (!PackDeviceFunction(name, error)) {
    throw std::runtime_error("PackDeviceFunction('" + name + "') failed: " + error);
  }
}

FullDeck::DeviceFunctionHandle FullDeck::GetDeviceFunction(const std::string &name) const {
  auto sit = device_modules_.find(name);
  auto eit = device_entry_ids_.find(name);
  if (sit == device_modules_.end() || eit == device_entry_ids_.end()) {
    throw std::runtime_error("Device function '" + name +
                             "' is not packed; call PackDeviceFunction first.");
  }
  DeviceFunctionHandle h;
  h.module = sit->second.view();
  h.entry_id = eit->second;
  return h;
}

double FullDeck::CallDeviceFunction(const std::string &name,
                                 const std::vector<double> &args) const {
  DeviceFunctionHandle h = GetDeviceFunction(name);
  std::vector<pips::device::DeviceValue> dargs;
  dargs.reserve(args.size());
  for (double a : args)
    dargs.push_back(pips::device::dv_number(static_cast<pips::device::DeviceReal>(a)));
  pips::device::DeviceVM dvm;
  pips::device::DeviceValue result{};
  pips::device::DeviceStatus st = dvm.run(
      h.module, h.entry_id, dargs.empty() ? nullptr : dargs.data(),
      static_cast<std::uint32_t>(dargs.size()), &result);
  if (st != pips::device::DeviceStatus::OK) {
    throw std::runtime_error("Device function '" + name +
                             "' failed with device status " +
                             std::to_string(static_cast<int>(st)));
  }
  if (result.type != pips::device::DeviceValueType::NUMBER) {
    throw std::runtime_error("Device function '" + name +
                             "' did not return a number.");
  }
  return static_cast<double>(result.as.n);
}

// ---------------------------------------------------------------------------
// Class introspection
// ---------------------------------------------------------------------------
std::string FullDeck::GetClassName(const std::string &suit) const {
  for (const auto &[inst, sp] : instance_to_suit_) {
    if (sp != suit) continue;
    if (inst && inst->classDef) return inst->classDef->name;
    break;
  }
  // Fall back to the static suit->class map (populated for `<X(inst)>`
  // headers even before any live instance is created).
  auto it = suit_class_name_.find(suit);
  if (it != suit_class_name_.end()) return it->second;
  // For "plain" suits the class name is the suit's mangled segment.
  if (!suit.empty() && std::find(suits.begin(), suits.end(), suit) != suits.end())
    return SuitClassName(suit);
  return "";
}

std::vector<std::string>
FullDeck::FindSuitsOfClass(const std::string &className) const {
  std::vector<std::string> out;
  for (const auto &[inst, sp] : instance_to_suit_) {
    if (inst && inst->classDef && inst->classDef->name == className) {
      out.push_back(sp);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

// ---------------------------------------------------------------------------
// Linked-tree (DeckGraph) representation
// ---------------------------------------------------------------------------
DeckGraph FullDeck::BuildGraph() const {
  DeckGraph g;
  // Recursively materialize a pips Instance into a Class node.  `visited`
  // breaks accidental cycles if the user produced a self-referential VM
  // graph; in practice instance graphs are trees.  Ordering prefers the
  // existing deck metadata (`card_map` + `suits`) rather than the VM's
  // unordered field/globals tables.
  std::function<std::unique_ptr<DeckNode>(const std::string &, pips::Instance *,
                                           const std::string &,
                                           std::set<pips::Instance *> &)>
      make_class =
          [&](const std::string &nm, pips::Instance *inst,
              const std::string &sp,
              std::set<pips::Instance *> &visited) -> std::unique_ptr<DeckNode> {
    auto node = std::make_unique<DeckNode>();
    node->kind = NodeKind::Class;
    node->name = nm;
    if (inst && inst->classDef) {
      node->class_name = inst->classDef->name;
      node->class_def = inst->classDef;
    }
    if (!inst || !visited.insert(inst).second) return node;

    std::set<std::string> emitted;
    auto emit_field = [&](const std::string &fname, const std::string &child_sp) {
      if (!emitted.insert(fname).second) return;
      auto fit = inst->fields.find(fname);
      if (fit == inst->fields.end()) return;
      const auto &fv = fit->second;
      if (fv.type == pips::ValueType::NIL) return;
      if (fv.type == pips::ValueType::INSTANCE) {
        auto child = make_class(fname, fv.as.instance, child_sp, visited);
        child->parent = node.get();
        node->children.push_back(std::move(child));
      } else {
        auto var = std::make_unique<DeckNode>();
        var->kind = NodeKind::Variable;
        var->name = fname;
        var->value = fv;
        var->parent = node.get();
        node->children.push_back(std::move(var));
      }
    };

    auto cm_it = card_map.find(sp);
    if (cm_it != card_map.end()) {
      for (const auto &fname : cm_it->second) emit_field(fname, "");
    }
    const std::string prefix = sp.empty() ? std::string{} : sp + "/";
    for (const auto &child_sp : suits) {
      if (prefix.empty() || child_sp.rfind(prefix, 0) != 0) continue;
      const std::string leaf = child_sp.substr(prefix.size());
      if (leaf.empty() || leaf.find('/') != std::string::npos) continue;
      emit_field(leaf, child_sp);
    }
    if (inst->classDef) {
      for (const auto &fname : inst->classDef->fields) {
        emit_field(fname, prefix + fname);
      }
    }
    for (const auto &[fname, _] : inst->fields) {
      emit_field(fname, prefix + fname);
    }
    return node;
  };

  auto &root = g.MutableRoot();
  std::set<pips::Instance *> visited;
  auto emit_global = [&](const std::string &gn, const pips::Value &gv,
                         const std::string &sp) {
    if (gn.empty() || gn[0] == '_') return;
    if (gv.type == pips::ValueType::INSTANCE) {
      auto child = make_class(gn, gv.as.instance, sp, visited);
      child->parent = &root;
      root.children.push_back(std::move(child));
    } else if (gv.type == pips::ValueType::NUMBER ||
               gv.type == pips::ValueType::STRING ||
               gv.type == pips::ValueType::BOOL ||
               gv.type == pips::ValueType::NIL ||
               gv.type == pips::ValueType::VECTOR) {
      auto var = std::make_unique<DeckNode>();
      var->kind = NodeKind::Variable;
      var->name = gn;
      var->value = gv;
      var->parent = &root;
      root.children.push_back(std::move(var));
    }
  };
  std::set<std::string> emitted;
  auto globals_it = card_map.find("/");
  if (globals_it != card_map.end()) {
    for (const auto &gn : globals_it->second) {
      auto it = vm.globals.find(gn);
      if (it == vm.globals.end()) continue;
      emit_global(gn, it->second, "/");
      emitted.insert(gn);
    }
  }
  for (const auto &sp : suits) {
    if (sp.empty() || sp == "/") continue;
    auto parts = SplitSuitPath(sp);
    if (parts.size() != 1) continue;
    auto it_name = suit_to_vm_name_.find(sp);
    const std::string vm_name =
        (it_name != suit_to_vm_name_.end()) ? it_name->second : parts[0];
    auto it = vm.globals.find(vm_name);
    if (it == vm.globals.end()) continue;
    emit_global(vm_name, it->second, sp);
    emitted.insert(vm_name);
  }
  for (const auto &[gn, gv] : vm.globals) {
    if (emitted.count(gn)) continue;
    emit_global(gn, gv, gn);
  }
  return g;
}

void FullDeck::PrintGraph(std::ostream &os) const { BuildGraph().Print(os); }

void FullDeck::SaveGraph(std::ostream &os) const { BuildGraph().Save(os); }

void FullDeck::LoadGraph(std::istream &is) { Build(is); }

} // namespace Rummy
