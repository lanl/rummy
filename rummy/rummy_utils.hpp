//========================================================================================
// (C) (or copyright) 2025. Triad National Security, LLC. All rights reserved.
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

#ifndef RUMMY_UTILS_HPP_
#define RUMMY_UTILS_HPP_

#include <algorithm>
#include <sstream>
#include <string>
namespace Rummy {

inline void fatal(const char *msg) {
  std::fprintf(stderr, "RUMMY: Fatal error: %s\n", msg);
  std::abort();
}
inline void fatal(const std::string &msg) {
  std::fprintf(stderr, "RUMMY: Fatal error: %s\n", msg.c_str());
  std::abort();
}
inline void fatal(const std::stringstream &msg) {
  std::fprintf(stderr, "RUMMY: Fatal error: %s\n", msg.str().c_str());
  std::abort();
}

inline void EmptyCheck(const std::string &str, const int line_num) {
  if (str.empty()) {
    std::stringstream msg;
    msg << "Empty string at line " << line_num;
    fatal(msg);
  }
}

inline void RemoveLeadingWhitespace(std::string &str) {
  str.erase(str.begin(),
            std::find_if(str.begin(), str.end(), [](char c) { return !std::isspace(c); }));
}

inline void RemoveTrailingWhitespace(std::string &str) {
  str.erase(std::find_if(str.rbegin(), str.rend(), [](char c) { return !std::isspace(c); })
                .base(),
            str.end());
}

inline void RemoveWhitespace(std::string &str) {
  str.erase(
      std::remove_if(str.begin(), str.end(), [](char c) { return std::isspace(c); }),
      str.end());
}

inline void RemoveWhitespacePreserveQuotes(std::string &str, const int line_num,
                                           char quote_char = '"') {
  auto quote_pos = str.find_first_of(quote_char);
  if (quote_pos == std::string::npos) {
    RemoveWhitespace(str);
    return;
  }
  auto end_quote_pos = str.find_first_of(quote_char, quote_pos + 1);
  if (end_quote_pos == std::string::npos) {
    std::stringstream msg;
    msg << "Missing closing quote in card value at line " << line_num;
    fatal(msg);
  }
  std::string before_quote = str.substr(0, quote_pos);
  RemoveWhitespace(before_quote);
  std::string quoted_value = str.substr(quote_pos, end_quote_pos - quote_pos + 1);
  std::string after_quote = str.substr(end_quote_pos + 1);
  RemoveWhitespace(after_quote);
  str = before_quote + quoted_value + after_quote;
}

inline std::vector<std::string> SplitString(const std::string &str, const int line_num,
                                            const int max_size = -1) {
  std::vector<std::string> vec;
  auto colon_pos = str.find_first_of(':');
  if (colon_pos == std::string::npos) {
    // setting a vector
    auto contents = str.substr(1, str.size() - 2); // remove brackets
    std::stringstream ss(contents);
    std::string value;
    while (std::getline(ss, value, ',')) {
      RemoveWhitespacePreserveQuotes(value, line_num);
      EmptyCheck(value, line_num);
      vec.push_back(value);
    }
  } else {

    // prefix[1:3]suffix[:2]suffix2
    // split this as
    // prefix[1]suffix[0]suffix2, prefix[2]suffix[1]suffix2,
    std::vector<std::string> parts;
    std::vector<int> lowers;
    std::vector<int> uppers;
    std::string current_part;
    std::string slice;
    bool in_brackets = false;
    for (char c : str) {
      if (c == '[') {
        if (!current_part.empty()) {
          parts.push_back(current_part);
          current_part.clear();
        }
        in_brackets = true;
      } else if (in_brackets) {
        if (c == ':') {
          if (!slice.empty()) {
            // we have a slice like [1:2]
            lowers.push_back(std::stoi(slice));
            slice.clear();
          } else {
            // if we have an empty slice, it means we have a slice like [:2]
            lowers.push_back(0);
          }
        } else if (c == ']') {
          if (!slice.empty()) {
            // we have a slice like [1:2]
            uppers.push_back(std::stoi(slice));
            slice.clear();
          } else {
            // if we have an empty slice, it means we have a slice like [1:]
            if (max_size < 0) {
              std::stringstream msg;
              msg << "Must specify upper bound in vector slice declaration at line "
                  << line_num;
              fatal(msg);
            }
            uppers.push_back(max_size);
            slice.clear();
          }
          in_brackets = false;
        } else {
          slice += c;
        }
      } else {
        current_part += c;
      }
    }

    if (!current_part.empty()) {
      parts.push_back(current_part);
    }

    // combine parts with slices
    int count = 99999;
    for (int i = 0; i < lowers.size(); i++) {
      count = std::min(count, uppers[i] - lowers[i]);
    }
    for (int i = 0; i < count; i++) {
      std::string contents;
      for (int j = 0; j < parts.size(); j++) {
        contents += parts[j] + "[" + std::to_string(lowers[j] + i) + "]";
      }
      RemoveWhitespacePreserveQuotes(contents, line_num);
      vec.push_back(contents);
    }
  }
  return vec;
}

} // namespace Rummy

#endif // RUMMY_UTILS_HPP_
