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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "deck.hpp"
#include "rummy_utils.hpp"
#include <pips/vm.hpp>

namespace Rummy {

namespace {

bool NeedsVmSuitAlias(const std::string &segment) {
  static const std::set<std::string> reserved = {
      "and",   "class",   "else",   "false",  "for",    "fn",    "if",
      "nil",   "not",     "new",    "or",     "print",  "return", "super",
      "this",  "true",    "var",    "while",  "getattr", "setattr",
      "pi",    "min",     "max",    "exp",    "sin",    "cos",   "tan",
      "abs",   "log",     "log10",  "sign",   "sqrt",   "acos",  "asin",
      "atan",  "atan2",   "ceil",   "floor",  "env",    "str"};

  if (segment.empty() || reserved.count(segment) > 0) {
    return true;
  }
  if (!(std::isalpha(static_cast<unsigned char>(segment.front())) ||
        segment.front() == '_')) {
    return true;
  }
  return std::any_of(segment.begin(), segment.end(), [](char c) {
    return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_');
  });
}

std::string VmSuitSegmentName(const std::string &segment) {
  if (!NeedsVmSuitAlias(segment)) {
    return segment;
  }

  std::string aliased = "__rummy_suit_";
  for (char c : segment) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      aliased += c;
    } else {
      aliased += '_';
    }
  }
  return aliased;
}

std::vector<std::string> SplitSuitPath(const std::string &suit_path) {
  std::vector<std::string> parts;
  if (suit_path.empty() || suit_path == "/") {
    return parts;
  }

  std::stringstream ss(suit_path);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  return parts;
}

std::string SuitObjectPath(const std::string &suit_path) {
  auto parts = SplitSuitPath(suit_path);
  std::string object_path;
  for (size_t idx = 0; idx < parts.size(); ++idx) {
    if (idx > 0) {
      object_path += ".";
    }
    object_path += VmSuitSegmentName(parts[idx]);
  }
  return object_path;
}

std::string ExternalSuitObjectPath(const std::string &suit_path) {
  std::string object_path = suit_path;
  std::replace(object_path.begin(), object_path.end(), '/', '.');
  return object_path;
}

std::string SuitClassName(const std::string &suit_path) {
  std::string class_name = "RummySuit";
  for (char c : suit_path) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      class_name += c;
    } else {
      class_name += '_';
    }
  }
  return class_name;
}

void EnsureClassField(pips::ClassDef &class_def, const std::string &field_name) {
  if (std::find(class_def.fields.begin(), class_def.fields.end(), field_name) ==
      class_def.fields.end()) {
    class_def.fields.push_back(field_name);
  }
}

pips::Instance *CreateInstance(pips::VM &vm, const std::string &class_name) {
  auto class_it = vm.classes.find(class_name);
  if (class_it == vm.classes.end()) {
    class_it = vm.classes.emplace(class_name, pips::ClassDef{}).first;
    class_it->second.name = class_name;
  }

  vm.instances.push_back(std::make_unique<pips::Instance>());
  pips::Instance *instance = vm.instances.back().get();
  instance->classDef = &class_it->second;
  for (const auto &field_name : instance->classDef->fields) {
    instance->fields[field_name] = pips::Value();
  }
  return instance;
}

pips::Instance *EnsureSuitHierarchy(pips::VM &vm, const std::string &suit_path) {
  auto parts = SplitSuitPath(suit_path);
  if (parts.empty()) {
    return nullptr;
  }

  std::string current_suit;
  std::string current_object_path;
  pips::Instance *current_instance = nullptr;

  for (size_t idx = 0; idx < parts.size(); ++idx) {
    const std::string &part = parts[idx];
    if (!current_suit.empty()) {
      current_suit += "/";
      current_object_path += ".";
    }
    current_suit += part;
    current_object_path += VmSuitSegmentName(part);

    const std::string class_name = SuitClassName(current_suit);
    auto class_it = vm.classes.find(class_name);
    if (class_it == vm.classes.end()) {
      class_it = vm.classes.emplace(class_name, pips::ClassDef{}).first;
      class_it->second.name = class_name;
    }

    if (idx == 0) {
      const std::string global_name = VmSuitSegmentName(part);
      auto global_it = vm.globals.find(global_name);
      if (global_it == vm.globals.end() ||
          global_it->second.type != pips::ValueType::INSTANCE) {
        pips::Instance *instance = CreateInstance(vm, class_name);
        vm.globals[global_name] = pips::Value(instance);
        current_instance = instance;
      } else {
        current_instance = global_it->second.as.instance;
      }
      continue;
    }

    const std::string field_name = VmSuitSegmentName(part);
    EnsureClassField(*current_instance->classDef, field_name);
    auto field_it = current_instance->fields.find(field_name);
    if (field_it == current_instance->fields.end() ||
        field_it->second.type != pips::ValueType::INSTANCE) {
      pips::Instance *child_instance = CreateInstance(vm, class_name);
      current_instance->fields[field_name] = pips::Value(child_instance);
      current_instance = child_instance;
    } else {
      current_instance = field_it->second.as.instance;
    }
  }

  return current_instance;
}

bool TryLookupValue(const pips::VM &vm, const std::string &path, pips::Value &value) {
  auto dot_pos = path.find('.');
  if (dot_pos == std::string::npos) {
    auto global_it = vm.globals.find(path);
    if (global_it == vm.globals.end()) {
      return false;
    }
    value = global_it->second;
    return true;
  }

  std::string root_name = path.substr(0, dot_pos);
  auto global_it = vm.globals.find(VmSuitSegmentName(root_name));
  if (global_it == vm.globals.end() ||
      global_it->second.type != pips::ValueType::INSTANCE) {
    return false;
  }

  pips::Instance *instance = global_it->second.as.instance;
  size_t segment_start = dot_pos + 1;
  while (segment_start < path.size()) {
    size_t next_dot = path.find('.', segment_start);
    std::string segment = path.substr(segment_start, next_dot - segment_start);

    const std::string field_name =
        (next_dot == std::string::npos) ? segment : VmSuitSegmentName(segment);
    auto field_it = instance->fields.find(field_name);
    if (field_it == instance->fields.end()) {
      return false;
    }

    value = field_it->second;
    if (next_dot == std::string::npos) {
      return true;
    }
    if (value.type != pips::ValueType::INSTANCE) {
      return false;
    }

    instance = value.as.instance;
    segment_start = next_dot + 1;
  }

  return false;
}

void SetSuitFieldValue(pips::VM &vm, const std::string &suit_path,
                       const std::string &field_name, const pips::Value &value) {
  pips::Instance *instance = EnsureSuitHierarchy(vm, suit_path);
  if (instance == nullptr) {
    return;
  }

  EnsureClassField(*instance->classDef, field_name);
  instance->fields[field_name] = value;
}

std::string CardPath(const std::string &suit, const std::string &card_name) {
  if (suit.empty() || suit == "/") {
    return card_name;
  }
  return ExternalSuitObjectPath(suit) + "." + card_name;
}

std::string VmPathForCardPath(const std::string &path) {
  const auto last_dot = path.find_last_of('.');
  if (last_dot == std::string::npos) {
    return path;
  }

  std::string suit_path = path.substr(0, last_dot);
  std::replace(suit_path.begin(), suit_path.end(), '.', '/');
  return SuitObjectPath(suit_path) + "." + path.substr(last_dot + 1);
}

} // namespace

void Deck::Build(std::string fname, std::string prepends) {
  std::stringstream pss;
  pss << prepends;
  Build(pss);
  std::ifstream input(fname);
  if (input.is_open()) {
    std::string base_dir = std::filesystem::path(fname).parent_path().string();
    std::stringstream ss;
    ss << input.rdbuf();
    BuildInternal(ss, base_dir);
  } else {
    std::stringstream msg;
    msg << "Could not open file '" << fname << "'";
    fatal(msg);
  }
}

void Deck::Build(std::istream &ss, std::string prepends) {
  std::stringstream pss;
  pss << prepends;
  Build(pss);
  Build(ss);
}

void Deck::Build(std::istream &ss, std::istream &prepends) {
  Build(prepends);
  Build(ss);
}

void Deck::CompileStream(std::istream &ss, std::map<std::string, CardMeta> &meta,
                         const std::string &base_dir,
                         std::set<std::string> &include_stack, pips::VTable &locals,
                         std::string &curr_suit, std::string &prev_suit) {
  auto lookupValueOrFatal = [&](const std::string &path, int line_num) {
    pips::Value value;
    if (!TryLookupValue(vm, path, value)) {
      std::stringstream msg;
      msg << "Failed to resolve card path '" << path << "' at line " << line_num;
      fatal(msg);
    }
    return value;
  };

  auto translateSuitPathsForVm = [&](const std::string &expr) {
    std::string translated;
    bool in_quotes = false;
    size_t idx = 0;
    while (idx < expr.size()) {
      char c = expr[idx];
      if (c == '"') {
        in_quotes = !in_quotes;
        translated += c;
        idx++;
        continue;
      }

      if (!in_quotes && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
        size_t end = idx + 1;
        while (end < expr.size()) {
          char tc = expr[end];
          if (std::isalnum(static_cast<unsigned char>(tc)) || tc == '_' || tc == '.' ||
              tc == '[' || tc == ']' || tc == ':') {
            end++;
            continue;
          }
          break;
        }

        std::string token = expr.substr(idx, end - idx);
        std::string replacement = token;
        std::string best_suit;
        size_t dot = token.find('.');
        while (dot != std::string::npos) {
          std::string prefix = token.substr(0, dot);
          std::string suit_candidate = prefix;
          std::replace(suit_candidate.begin(), suit_candidate.end(), '.', '/');
          if (deck.find(suit_candidate) != deck.end() && suit_candidate != "/") {
            best_suit = suit_candidate;
          }
          dot = token.find('.', dot + 1);
        }
        if (best_suit.empty() && deck.find(token) != deck.end() && token != "/") {
          best_suit = token;
        }
        if (!best_suit.empty()) {
          const std::string external_prefix = ExternalSuitObjectPath(best_suit);
          replacement = SuitObjectPath(best_suit) + token.substr(external_prefix.size());
        }

        translated += replacement;
        idx = end;
        continue;
      }

      translated += c;
      idx++;
    }
    return translated;
  };

  std::string line;
  std::string comment;
  std::string multiline;
  bool line_continue = false;

  int line_num = 0;
  while (std::getline(ss, line)) {
    line_num++;
    // remove all \t\f\n\r\v but leave pure spaces in case of a string containing spaces
    line.erase(std::remove_if(line.begin(), line.end(),
                              [](char c) { return std::isspace(c) && c != ' '; }),
               line.end());

    if (line.empty()) continue;                          // skip blank line
    auto first_char = line.find_first_not_of(" ");       // skip white space
    if (first_char == std::string::npos) continue;       // line is all white space
    if (line.compare(first_char, 1, "#") == 0) continue; // skip comments
    // remove trailing comments — skip '#' that appears inside a quoted string
    std::string this_comment;
    {
      bool in_quotes = false;
      size_t last_comment = std::string::npos;
      for (size_t ci = 0; ci < line.size(); ++ci) {
        if (line[ci] == '"') {
          in_quotes = !in_quotes;
        } else if (line[ci] == '#' && !in_quotes) {
          last_comment = ci;
          break;
        }
      }
      if (last_comment != std::string::npos) {
        // preserve the comment
        this_comment = line.substr(last_comment + 1);
        line = line.substr(0, last_comment);
        this_comment.erase(std::remove(this_comment.begin(), this_comment.end(), '&'),
                           this_comment.end());
        RemoveLeadingWhitespace(this_comment);
        RemoveTrailingWhitespace(this_comment);
        if (line_continue && !this_comment.empty()) {
          comment += " " + this_comment;
        } else if (!line_continue) {
          comment = this_comment;
        }
      }
    } // end quote-aware comment search
    // the multiline character has to be the last character of the line
    // once comments and whitespace are removed
    auto last_char = line.find_last_not_of(" ");
    if ((last_char == std::string::npos) || (last_char < first_char)) continue;
    if (line[last_char] == '&') {
      // if we have a multiline character, then we need to continue the line
      if (line_continue) {
        // if we are continuing a multiline, then we need to add the line to the
        // multiline
        multiline += " " + line.substr(first_char, last_char - first_char);
      } else {
        // start a new multiline
        multiline = line.substr(first_char, last_char - first_char);
        line_continue = true;
      }
      continue;
    } else {
      // if we have a multiline character, then we need to add it to the multiline
      // string
      if (line_continue) {
        // close out the multiline
        multiline += " " + line.substr(first_char, last_char - first_char + 1);

        // move multiline into line and reset the first character of line
        line = multiline;
        first_char = line.find_first_not_of(" ");

        multiline.clear();
        line_continue = false;
      }
    }

    // include statement
    if (line.compare(first_char, 7, "include") == 0) {
      const size_t after_kw = first_char + 7;
      const size_t quote_open = line.find_first_not_of(" ", after_kw);
      if (quote_open != std::string::npos && line[quote_open] == '"') {
        auto quote_close = line.find('"', quote_open + 1);
        if (quote_close == std::string::npos) {
          std::stringstream msg;
          msg << "Malformed include statement at line " << line_num;
          fatal(msg);
        }
        std::string inc_path = line.substr(quote_open + 1, quote_close - quote_open - 1);
        if (inc_path.empty()) {
          std::stringstream msg;
          msg << "Empty filename in include statement at line " << line_num;
          fatal(msg);
        }
        std::filesystem::path resolved(inc_path);
        if (resolved.is_relative() && !base_dir.empty()) {
          resolved = std::filesystem::path(base_dir) / resolved;
        }
        std::error_code ec;
        auto canonical = std::filesystem::canonical(resolved, ec);
        if (ec) {
          std::stringstream msg;
          msg << "Cannot resolve include file '" << inc_path << "' at line " << line_num;
          fatal(msg);
        }
        const std::string canonical_str = canonical.string();
        if (include_stack.count(canonical_str)) {
          std::stringstream msg;
          msg << "Circular include detected: '" << inc_path << "' at line " << line_num;
          fatal(msg);
        }
        std::ifstream inc_stream(canonical_str);
        if (!inc_stream.is_open()) {
          std::stringstream msg;
          msg << "Cannot open include file '" << inc_path << "' at line " << line_num;
          fatal(msg);
        }
        include_stack.insert(canonical_str);
        const std::string inc_base_dir = canonical.parent_path().string();
        CompileStream(inc_stream, meta, inc_base_dir, include_stack, locals, curr_suit,
                      prev_suit);
        include_stack.erase(canonical_str);
        continue;
      }
    } // include statement

    // start of a new suit
    // TODO define the start and end characters in cmake
    if (line.compare(first_char, 1, "<") == 0) {
      auto last_char = line.find_first_of(">");
      if (last_char == std::string::npos) {
        std::stringstream msg;
        msg << "Missing '>' in suit declaration at line " << line_num;
        fatal(msg);
      }
      std::string suit_name = line.substr(first_char + 1, last_char - first_char - 1);
      RemoveWhitespace(suit_name);
      if (suit_name.empty()) {
        std::stringstream msg;
        msg << "Empty suit name at line " << line_num;
        fatal(msg);
      } else if (suit_name.compare(0, 2, "..") == 0) {
        // replace .. with current suit name
        // don't update previous suit
        if (prev_suit.empty()) {
          std::stringstream msg;
          msg << "Cannot use '..' in suit name at line " << line_num;
          fatal(msg);
        }
        suit_name = prev_suit + suit_name.substr(2);
        curr_suit = suit_name;
      } else {
        curr_suit = suit_name;
        prev_suit = curr_suit;
      }
      if (deck.find(curr_suit) == deck.end()) {
        deck[curr_suit] = std::map<std::string, Card>();
        suits.push_back(curr_suit);
        card_map[curr_suit] = std::vector<std::string>();
      }
      EnsureSuitHierarchy(vm, curr_suit);
      locals.clear();
      continue;
    }

    // Actual card line
    // split the line into card = val
    // Find the first '=' that is not inside a quoted string
    auto eq_char = std::string::npos;
    {
      bool in_quotes = false;
      for (size_t i = first_char; i < line.size(); ++i) {
        if (line[i] == '"')
          in_quotes = !in_quotes;
        else if (!in_quotes && line[i] == '=') {
          eq_char = i;
          break;
        }
      }
    }
    if (eq_char == std::string::npos) {
      std::string statement = line.substr(first_char, last_char - first_char + 1);
      if (statement == "__globals__") {
        std::printf("Globals:\n");
        std::set<std::string> printed;
        for (const auto &[path, card_meta] : meta) {
          pips::Value value;
          if (!TryLookupValue(vm, path, value) ||
              value.type == pips::ValueType::INSTANCE) {
            continue;
          }
          std::printf("  %s = ", path.c_str());
          pips::printValue(value);
          std::printf("\n");
          printed.insert(path);
        }
        for (const auto &[name, value] : vm.globals) {
          if (value.type == pips::ValueType::INSTANCE || printed.count(name) > 0) {
            continue;
          }
          std::printf("  %s = ", name.c_str());
          pips::printValue(value);
          std::printf("\n");
        }
        for (const auto &[name, value] : locals) {
          if (value.type == pips::ValueType::INSTANCE || printed.count(name) > 0) {
            continue;
          }
          std::printf("  %s = ", name.c_str());
          pips::printValue(value);
          std::printf("\n");
        }
        continue;
      }
      // this is a pips statement
        const std::string translated_statement = translateSuitPathsForVm(statement);
        if (vm.interpret(translated_statement.c_str(), '\n', locals) !=
          pips::InterpretResult::OK) {
        std::stringstream msg;
        msg << "Failed to compile expression '" << statement << "' at line "
            << line_num;
        msg << "\nPossibly missing '=' in card declaration.";
        fatal(msg);
      }
      continue;
    }

    std::string local_name = line.substr(first_char, eq_char - first_char);
    // remove whitespace from local_name
    RemoveWhitespace(local_name);
    EmptyCheck(local_name, line_num);

    // add card name to suit list
    // Strip any [...] suffix so that slice assignments like v[:3] are stored
    // under the base name "v", matching the individual element cards v[0], v[1], ...
    // Globals have an empty curr_suit but are stored under "/" in the deck.

    std::string card_value = line.substr(eq_char + 1);
    EmptyCheck(card_value, line_num);
    // Trim leading/trailing whitespace only — preserve internal spacing
    card_value.erase(0, card_value.find_first_not_of(" \t\r\n"));
    card_value.erase(card_value.find_last_not_of(" \t\r\n") + 1);
    // Strip whitespace only from the parts outside quoted strings for the
    // string-value case; the raw card_value is kept for expressions.
    std::string card_value_stripped = card_value;
    RemoveWhitespacePreserveQuotes(card_value_stripped, line_num);
    EmptyCheck(card_value, line_num);
    std::string global_name;
    std::string name_prefix;
    std::string object_path;
    if (curr_suit.empty()) {
      // no suit, use local name as global name
      global_name = local_name;

      // standalone variable but need to identify suit
      if (local_name.find('.') != std::string::npos) {
        auto dot_pos = local_name.find_last_of('.');
        std::string suit_name = local_name.substr(0, dot_pos);
        local_name = local_name.substr(dot_pos + 1, std::string::npos);
        std::replace(suit_name.begin(), suit_name.end(), '.', '/');
        curr_suit = suit_name;
        object_path = SuitObjectPath(curr_suit);
        name_prefix = ExternalSuitObjectPath(curr_suit) + ".";
        if (deck.find(curr_suit) == deck.end()) {
          deck[curr_suit] = std::map<std::string, Card>();
          suits.push_back(curr_suit);
          card_map[curr_suit] = std::vector<std::string>();
        }
        EnsureSuitHierarchy(vm, curr_suit);
        global_name = name_prefix + local_name;
      }
    } else {
      // standalone variable needs to reset curr_suit
      if (local_name.find('.') != std::string::npos) {
        auto dot_pos = local_name.find_last_of('.');
        std::string suit_name = local_name.substr(0, dot_pos);
        local_name = local_name.substr(dot_pos + 1, std::string::npos);
        std::replace(suit_name.begin(), suit_name.end(), '.', '/');
        curr_suit = suit_name;
        object_path = SuitObjectPath(curr_suit);
        name_prefix = ExternalSuitObjectPath(curr_suit) + ".";
        global_name = name_prefix + local_name;
        if (deck.find(curr_suit) == deck.end()) {
          deck[curr_suit] = std::map<std::string, Card>();
          suits.push_back(curr_suit);
          card_map[curr_suit] = std::vector<std::string>();
        }
        EnsureSuitHierarchy(vm, curr_suit);
      } else {
        object_path = SuitObjectPath(curr_suit);
        name_prefix = ExternalSuitObjectPath(curr_suit) + ".";
        global_name = name_prefix + local_name;
      }
    }

    // Variable updates
    {
      auto lb = local_name.find('[');
      bool is_dotted = (local_name.find('.') != std::string::npos);
      bool is_slice = is_dotted && (lb != std::string::npos) &&
                      (local_name.find(':', lb) != std::string::npos);
      std::string slice_base = is_slice ? local_name.substr(0, lb) : "";
      bool is_dotted_update =
          is_dotted &&
          ([&]() {
            pips::Value ignored;
            return TryLookupValue(vm, local_name, ignored) ||
                   (is_slice && TryLookupValue(vm, slice_base + "[0]", ignored));
          })();
      if (is_dotted_update) {
        if (is_slice) {
          auto expanded_names = SplitString(local_name, line_num);
          auto expanded_values = SplitString(card_value_stripped, line_num);
          if (expanded_names.size() > expanded_values.size()) {
            std::stringstream msg;
            msg << "More slice targets than values in dotted assignment at line "
                << line_num;
            fatal(msg);
          }
          for (size_t idx = 0; idx < expanded_names.size(); idx++) {
            const std::string &ename = expanded_names[idx];
            const std::string &evalue = expanded_values[idx];
            std::string expr =
              VmPathForCardPath(ename) + " = " + translateSuitPathsForVm(evalue);
            if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
              std::stringstream msg;
              msg << "Failed to compile dotted slice assignment '" << expr << "' at line "
                  << line_num;
              fatal(msg);
            }
            locals[ename.c_str()] = lookupValueOrFatal(ename, line_num);
            meta[ename.c_str()] = {line_num, comment};
          }
          comment.clear();
          continue;
        }
        std::string expr =
          VmPathForCardPath(local_name) + " = " + translateSuitPathsForVm(card_value);
        if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
          std::stringstream msg;
          msg << "Failed to compile dotted assignment '" << expr << "' at line "
              << line_num;
          fatal(msg);
        }
        locals[local_name.c_str()] = lookupValueOrFatal(local_name, line_num);
        meta[local_name.c_str()] = {line_num, comment};
        comment.clear();
        continue;
      }
    }
    // Add this card to the card map
    {
      auto bracket = local_name.find('[');
      std::string base_name =
          (bracket != std::string::npos) ? local_name.substr(0, bracket) : local_name;
      const std::string &map_suit = curr_suit.empty() ? "/" : curr_suit;
      if (std::find(card_map[map_suit].begin(), card_map[map_suit].end(), base_name) ==
          card_map[map_suit].end()) {
        card_map[map_suit].push_back(base_name);
      }
    }

    // Processing the card
    // Four cases:
    //  a = 2           # no vector
    //  a = [1,2,3]     # assign a vector
    //  a[:2] = [1,2]   # assign a slice of a vector
    //  a[:2] = b[:2]   # vector operation

    bool lhs_vec = false;
    bool rhs_vec = false;
    // check for [] in the local name
    auto open_bracket = local_name.find_first_of('[');
    if (open_bracket != std::string::npos) {
      // we have a vector case
      auto close_bracket = local_name.find_first_of(']', open_bracket);
      if (close_bracket == std::string::npos) {
        std::stringstream msg;
        msg << "Missing closing ']' in vector declaration at line " << line_num;
        fatal(msg);
      }
      lhs_vec = true;
    }
    open_bracket = card_value_stripped.find_first_of('[');
    if (open_bracket != std::string::npos) {
      // we have a vector case
      auto close_bracket = card_value_stripped.find_first_of(']', open_bracket);
      if (close_bracket == std::string::npos) {
        std::stringstream msg;
        msg << "Missing closing ']' in vector declaration at line " << line_num;
        fatal(msg);
      }
      rhs_vec = true;
    } else {
      // allow vector without []. Look for comma separated values
      // a = 1,2,3
      // but ignore commas inside parentheses (e.g. atan2(a,b)) or quotes
      if (card_value_stripped.find_first_of(',') != std::string::npos) {
        auto comma_pos = card_value_stripped.find_first_of(',');
        while (comma_pos != std::string::npos) {
          bool in_quotes = false;
          int paren_depth = 0;
          for (size_t i = 0; i < comma_pos; i++) {
            char c = card_value_stripped[i];
            if (c == '"')
              in_quotes = !in_quotes;
            else if (!in_quotes && c == '(')
              paren_depth++;
            else if (!in_quotes && c == ')')
              paren_depth--;
          }
          if (!in_quotes && paren_depth == 0) {
            rhs_vec = true;
            break;
          }
          comma_pos = card_value_stripped.find_first_of(',', comma_pos + 1);
        }
      }
    }
    const bool has_comma = card_value_stripped.find_first_of(',') != std::string::npos;
    if (local_name.find_first_of(',') != std::string::npos) {
      std::stringstream msg;
      msg << "Cannot have comma in card name at line " << line_num;
      fatal(msg);
    }

    // A colon is a slice separator only when it appears inside [...] on the
    // LHS or RHS. A bare colon (e.g. from a ternary a ? b : c) is not a slice.
    bool has_colon = (local_name.find_first_of(':') != std::string::npos);
    if (!has_colon && rhs_vec) {
      // Only look for a colon if we already know we're in a vector context
      bool in_quotes = false;
      bool in_brackets = false;
      for (char c : card_value_stripped) {
        if (c == '"')
          in_quotes = !in_quotes;
        else if (!in_quotes && c == '[')
          in_brackets = true;
        else if (!in_quotes && c == ']')
          in_brackets = false;
        else if (!in_quotes && in_brackets && c == ':') {
          has_colon = true;
          break;
        }
      }
    }
    if (!has_colon && ((!lhs_vec && !rhs_vec) || (lhs_vec && !rhs_vec) ||
                       ((lhs_vec || rhs_vec) && !has_comma))) {
      // a = 2
      // a[0] = 2
      // a = b[0]
      std::string expr;
      if (curr_suit.empty()) {
        expr = "var " + global_name + " = " + translateSuitPathsForVm(card_value);
      } else {
        expr = "setattr(" + object_path + ", \"" + local_name + "\", " +
               translateSuitPathsForVm(card_value) + ")";
      }
      // add the local card to the locals table
      if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
        std::stringstream msg;
        msg << "Failed to compile expression '" << expr << "' at line " << line_num;
        fatal(msg);
      }
      auto value = lookupValueOrFatal(global_name, line_num);
      meta[global_name.c_str()] = {line_num, comment};
      comment.clear();
      // Stash the local for this suit
      locals[local_name.c_str()] = value;
    } else if (!lhs_vec && rhs_vec) {
      // a = [1,2,3]
      // loop through comma separated values
      auto open_bracket = card_value_stripped.find_first_of('[');
      if (open_bracket != std::string::npos) {
        auto close_bracket = card_value_stripped.find_first_of(']', open_bracket);
        card_value_stripped = card_value_stripped.substr(
            open_bracket + 1, close_bracket - open_bracket - 1);
      }

      // Split card_value_stripped by commas, but ignore commas inside quotes
      std::vector<std::string> values;
      std::string current;
      bool in_quotes = false;
      for (size_t i = 0; i < card_value_stripped.size(); ++i) {
        char c = card_value_stripped[i];
        if (c == '"') {
          in_quotes = !in_quotes;
          current += c;
        } else if (c == ',' && !in_quotes) {
          values.push_back(current);
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        values.push_back(current);
      }
      int index = 0;
      for (auto &value : values) {
        RemoveWhitespacePreserveQuotes(value, line_num);
        EmptyCheck(value, line_num);
        // add the local card to the locals table
        std::string vec_name = global_name + "[" + std::to_string(index) + "]";
        std::string expr;
        if (curr_suit.empty()) {
          expr = "var " + vec_name + " = " + translateSuitPathsForVm(value);
        } else {
          std::string field_name = local_name + "[" + std::to_string(index) + "]";
          expr = "setattr(" + object_path + ", \"" + field_name + "\", " +
                 translateSuitPathsForVm(value) + ")";
        }
        if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
          std::stringstream msg;
          msg << "Failed to compile expression '" << expr << "' at line " << line_num;
          fatal(msg);
        }
        auto vec_value = lookupValueOrFatal(vec_name, line_num);
        meta[vec_name.c_str()] = {line_num, comment};
        comment.clear();
        // Stash the local for this suit
        std::string local_vec_name = local_name + "[" + std::to_string(index) + "]";
        locals[local_vec_name.c_str()] = vec_value;
        index++;
      }
    } else {
      // a[1:2] = [1,2]
      // a[:2] = b[:2]
      // These are handled by replicating the line and substituting the indices
      auto card_values = SplitString(card_value_stripped, line_num);
      auto card_names = SplitString(local_name, line_num, card_values.size());

      // the RHS is split up
      // Now create each expression and evaluate it

      if (card_names.size() > card_values.size()) {
        std::stringstream msg;
        msg << "More card names than values at line " << line_num;
        fatal(msg);
      }
      for (size_t idx = 0; idx < card_names.size(); idx++) {
        std::string local_vec_name = card_names[idx];
        std::string global_vec_name = name_prefix + local_vec_name;

        std::string expr;
        if (curr_suit.empty()) {
          expr = "var " + global_vec_name + " = " +
                 translateSuitPathsForVm(card_values[idx]);
        } else {
          expr = "setattr(" + object_path + ", \"" + local_vec_name + "\", " +
                 translateSuitPathsForVm(card_values[idx]) + ")";
        }
        if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
          std::stringstream msg;
          msg << "Failed to compile expression '" << expr << "' at line " << line_num;
          fatal(msg);
        }
        auto value = lookupValueOrFatal(global_vec_name, line_num);
        meta[global_vec_name.c_str()] = {line_num, comment};
        comment.clear();
        // Stash the local for this suit
        locals[local_vec_name.c_str()] = value;
      }
    }

  } // end while
}

void Deck::CompileInput(std::istream &ss, std::map<std::string, CardMeta> &meta,
                        const std::string &base_dir) {
  pips::VTable locals;
  std::string curr_suit;
  std::string prev_suit;
  std::set<std::string> include_stack;
  CompileStream(ss, meta, base_dir, include_stack, locals, curr_suit, prev_suit);
}

void Deck::Build(std::istream &ss) { BuildInternal(ss, ""); }

void Deck::BuildInternal(std::istream &ss, const std::string &base_dir) {
  std::map<std::string, CardMeta> meta;

  if (!deck.empty()) {
    for (const auto &suit : deck) {
      for (const auto &card : suit.second) {
        if (suit.first == "/") {
          vm.globals[card.first] = card.second.GetValue();
          meta[card.first] = {card.second.loc, card.second.GetComment()};
        } else {
          SetSuitFieldValue(vm, suit.first, card.first, card.second.GetValue());
          meta[CardPath(suit.first, card.first)] = {card.second.loc, card.second.GetComment()};
        }
      }
    }
  }
  // Ensure the global "/" suit exists
  if (deck.find("/") == deck.end()) {
    deck["/"] = std::map<std::string, Card>();
    suits.push_back("/");
    card_map["/"] = std::vector<std::string>();
  }
  CompileInput(ss, meta, base_dir);

  for (const auto &[path, card_meta] : meta) {
    pips::Value value;
    if (!TryLookupValue(vm, path, value)) {
      continue;
    }
    const auto last_dot = path.find_last_of('.');
    std::string suit, card_name;
    if (last_dot == std::string::npos) {
      suit = "/";
      card_name = path;
    } else {
      suit = path.substr(0, last_dot);
      card_name = path.substr(last_dot + 1);
      std::replace(suit.begin(), suit.end(), '.', '/');
    }
    CopyCard(Card(suit, card_name, value, card_meta.comment, card_meta.loc));
  }
}

void Deck::RecompileCard(const std::string &line) {
  // The line should already be in the correct format
  // so we can pass it directly to compiler

  if (vm.interpret(line.c_str(), '\n') != pips::InterpretResult::OK) {
    std::stringstream msg;
    msg << "Failed to compile expression '" << line << "'";
    fatal(msg);
  }
  return;
}
void Deck::UpdateDeck(void) {
  // Update the table
  for (const auto &[suit_name, cards] : deck) {
    for (const auto &[card_name, card] : cards) {
      pips::Value value;
      if (!TryLookupValue(vm, CardPath(suit_name, card_name), value)) {
        continue;
      }
      if (DoesSuitExist(suit_name) && DoesCardExist(suit_name, card_name)) {
        UpdateCard(suit_name, card_name,
                   Card(suit_name, card_name, value, card.GetComment(), card.loc));
      }
    }
  }
  return;
}

void Deck::CopyVmState(const pips::VM &other_vm) {
  vm = pips::VM();
  vm.functions = other_vm.functions;
  vm.classes = other_vm.classes;

  std::unordered_map<const pips::Instance *, pips::Instance *> instance_map;
  vm.instances.reserve(other_vm.instances.size());
  for (const auto &source_instance : other_vm.instances) {
    auto copied_instance = std::make_unique<pips::Instance>();
    if (source_instance && source_instance->classDef) {
      auto class_it = vm.classes.find(source_instance->classDef->name);
      if (class_it != vm.classes.end()) {
        copied_instance->classDef = &class_it->second;
      }
    }
    instance_map[source_instance.get()] = copied_instance.get();
    vm.instances.push_back(std::move(copied_instance));
  }

  auto copyValue = [&](const pips::Value &source_value) {
    pips::Value copied_value(source_value);
    if (source_value.type == pips::ValueType::INSTANCE) {
      auto instance_it = instance_map.find(source_value.as.instance);
      if (instance_it != instance_map.end()) {
        copied_value.as.instance = instance_it->second;
      }
    }
    return copied_value;
  };

  for (size_t idx = 0; idx < other_vm.instances.size(); ++idx) {
    const auto &source_instance = other_vm.instances[idx];
    auto &copied_instance = vm.instances[idx];
    if (!source_instance || !copied_instance) {
      continue;
    }
    for (const auto &[field_name, field_value] : source_instance->fields) {
      copied_instance->fields[field_name] = copyValue(field_value);
    }
  }

  for (const auto &[name, value] : other_vm.globals) {
    vm.globals[name] = copyValue(value);
  }
}

Card &Deck::GetCard(const std::string &suit, const std::string &name) {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) {
    std::stringstream msg;
    msg << "Suit '" << suit << "' not found in the deck.";
    fatal(msg);
  }
  auto card_it = suit_it->second.find(name);
  if (card_it == suit_it->second.end()) {
    std::stringstream msg;
    msg << "Card '" << name << "' not found in suit '" << suit << "'.";
    fatal(msg);
  }
  return card_it->second;
}
void Deck::RemoveCard(const std::string &suit, const std::string &name) {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) {
    std::stringstream msg;
    msg << "Suit '" << suit << "' not found in the deck.";
    fatal(msg);
  }
  auto card_it = suit_it->second.find(name);
  if (card_it == suit_it->second.end()) {
    std::stringstream msg;
    msg << "Card '" << name << "' not found in suit '" << suit << "'.";
    fatal(msg);
  }
  suit_it->second.erase(card_it);
}

void Deck::UpdateCard(const std::string &suit, const std::string &name, const Card &card,
                      std::string comment) {
  auto &mycard = GetCard(suit, name);
  mycard = card;
  if (!comment.empty() && (comment != "")) {
    mycard.UpdateComment(comment);
  }
}
// functions to iterate over the deck
std::vector<std::string> Deck::GetCardsInOrder(const std::string &suit) const {
  if (card_map.find(suit) != card_map.end()) {
    return card_map.at(suit);
  }
  return {};
}
// FindSuit returns a map of cards that match the suit
std::map<std::string, Card> Deck::FindSuit(const std::string &suit) const {
  auto it = deck.find(suit);
  if (it == deck.end()) {
    std::stringstream msg;
    msg << "Suit '" << suit << "' not found in the deck.";
    fatal(msg);
  }
  return it->second;
}
// fuzzy match version of FindSuit
std::vector<Card> Deck::FindSuitFuzzy(std::string suit_) const {
  if (suit_ != "/") {
    // remove wildcard character '*' if present
    auto star_pos = suit_.find('*');
    if (star_pos != std::string::npos) {
      suit_.erase(star_pos, 1);
    }
  }
  std::vector<Card> result;
  for (const auto &suit : deck) {
    if (suit.first.find(suit_) != std::string::npos) {
      // use the loc as the sorting index
      for (const auto &card : suit.second) {
        result.push_back(card.second);
      }
    }
  }
  if (result.empty()) {
    std::cerr << "No suits matching '" << suit_ << "' found in the deck." << std::endl;
  }
  return result;
}
std::vector<Card> Deck::FindSuitInOrder(const std::string &suit, const bool fuzzy) const {
  std::vector<Card> subdeck;
  if (fuzzy) {
    subdeck = FindSuitFuzzy(suit);
  } else {
    const auto &block = deck.find(suit);
    if (block == deck.end()) {
      if (suit != "/") {
        std::stringstream msg;
        msg << "Suit '" << suit << "' not found in the deck.";
        fatal(msg);
      }
    } else {
      for (const auto &card : block->second) {
        subdeck.push_back(card.second);
      }
    }
  }
  std::sort(subdeck.begin(), subdeck.end(),
            [](const Card &a, const Card &b) { return a.loc < b.loc; });
  return subdeck;
}
std::vector<Card> Deck::FindCardFuzzy(std::string suit, std::string name) const {
  std::vector<Card> result;
  auto cards = FindSuit(suit);
  for (const auto &card : cards) {
    if (card.first.find(name) != std::string::npos) {
      result.push_back(card.second);
    }
  }
  return result;
}
bool Deck::DoesSuitExist(const std::string &suit) const {
  return deck.find(suit) != deck.end();
}
bool Deck::DoesCardExist(const std::string &suit, const std::string &name) const {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) return false;
  // Be careful of vectors
  return (suit_it->second.find(name) != suit_it->second.end()) ||
         (suit_it->second.find(name + "[0]") != suit_it->second.end());
}
bool Deck::IsCardVector(const std::string &suit, const std::string &name) const {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) return false;
  // one of the cards must be the first element
  for (const auto &card : suit_it->second) {
    if (card.first == name + "[0]") {
      return true;
    }
  }
  return false;
}
void Deck::WriteDeck(std::ostream &os) const {
  for (const auto &suit_name : suits) {
    if (deck.find(suit_name) == deck.end()) continue;
    if (!(suit_name.empty() || (suit_name == "/"))) {
      os << "<" << suit_name << ">\n";
    }
    const auto &suit = deck.at(suit_name);
    // Collect cards and sort by insertion order (loc), falling back to name for
    // cards added programmatically (loc == -1). This preserves forward-reference
    // correctness when the output is re-read.
    std::vector<std::pair<std::string, const Card *>> ordered;
    ordered.reserve(suit.size());
    for (const auto &card_pair : suit) {
      ordered.push_back({card_pair.first, &card_pair.second});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
      if (a.second->loc != b.second->loc) return a.second->loc < b.second->loc;
      return a.first < b.first;
    });
    for (const auto &entry : ordered) {
      const auto &card = *entry.second;
      const std::string &name = entry.first;
      os << name << " = ";
      if (card.isString()) {
        os << "\"" << card.GetString() << "\"";
      } else {
        os << card.GetString();
      }
      if (!card.GetComment().empty()) {
        os << "  # " << card.GetComment();
      }
      os << "\n";
    }
    os << "\n";
  }
}

} // namespace Rummy
