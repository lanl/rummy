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

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "deck.hpp"
#include "rummy_utils.hpp"
#include <pips/vm.hpp>

namespace Rummy {

void Deck::Build(std::string fname, std::string prepends) {
  std::stringstream pss;
  pss << prepends;
  Build(pss);
  std::ifstream input(fname);
  if (input.is_open()) {
    std::stringstream ss;
    ss << input.rdbuf();
    Build(ss);
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

void Deck::CompileInput(std::istream &ss, std::map<std::string, int> &locations, std::map<std::string, std::string> &comments) {
  pips::VTable locals;
  std::string line;
  std::string comment;
  std::string multiline;
  bool line_continue = false;

  int line_num = -1;
  std::string curr_suit;
  std::string prev_suit;
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
    // remove trailing comments
    auto last_comment = line.find_first_of("#");
    std::string this_comment;
    if (last_comment != std::string::npos) {
      // preserve the comment
      this_comment = line.substr(last_comment + 1);
      line = line.substr(0, last_comment);
      this_comment.erase(std::remove(this_comment.begin(), this_comment.end(), '&'), this_comment.end());
      RemoveLeadingWhitespace(this_comment);
      RemoveTrailingWhitespace(this_comment);
      if (line_continue && !this_comment.empty()) {
        comment += " " + this_comment;
      } else if (!line_continue) {
        comment = this_comment;
      }
    }
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
       // deck[curr_suit] = std::map<std::string, Card>();
        suits.push_back(curr_suit);
      }
      locals.clear();
      continue;
    }

    // Actual card line
    // split the line into card = val
    auto eq_char = line.find_first_of("=");
    if (eq_char == std::string::npos) {
      if (vm.interpret(line.c_str(), '\n', locals) != pips::InterpretResult::OK) {
        std::stringstream msg;
        msg << "Failed to compile expression '" << line << "' at line " << line_num;
        msg << "\nPossibly missing '=' in card declaration.";
        fatal(msg);
      }
      continue;
    }

    std::string local_name = line.substr(first_char, eq_char - first_char);
    // remove whitespace from local_name
    RemoveWhitespace(local_name);

    EmptyCheck(local_name, line_num);
    std::string card_value = line.substr(eq_char + 1);
    EmptyCheck(card_value, line_num);
    RemoveWhitespacePreserveQuotes(card_value, line_num);
    EmptyCheck(card_value, line_num);
    std::string global_name;
    std::string name_prefix;
    if (curr_suit.empty()) {
      // no suit, use local name as global name
      global_name = local_name;
    } else {
      std::string suit_card_name = curr_suit;
      std::replace(suit_card_name.begin(), suit_card_name.end(), '/', '.');
      // use suit name as prefix
      name_prefix = suit_card_name + ".";
      global_name = name_prefix + local_name;
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
    open_bracket = card_value.find_first_of('[');
    if (open_bracket != std::string::npos) {
      // we have a vector case
      auto close_bracket = card_value.find_first_of(']', open_bracket);
      if (close_bracket == std::string::npos) {
        std::stringstream msg;
        msg << "Missing closing ']' in vector declaration at line " << line_num;
        fatal(msg);
      }
      rhs_vec = true;
    } else {
      // allow vector without []. Look for comma separated values
      // a = 1,2,3
      // but ignore commas in strings
      if (card_value.find_first_of(',') != std::string::npos) {
        // a = "strings, with", "commas, in", "them"
        // loop through string, keeping track of being inside quotes, if a comma happens
        // outside, done
        auto comma_pos = card_value.find_first_of(',');
        while (comma_pos != std::string::npos) {
          bool in_quotes = false;
          for (size_t i = 0; i < comma_pos; i++) {
            if (card_value[i] == '"') {
              in_quotes = !in_quotes;
            }
          }
          if (!in_quotes) {
            rhs_vec = true;
            break;
          }
          comma_pos = card_value.find_first_of(',', comma_pos + 1);
        }
      }
    }
    const bool has_comma = card_value.find_first_of(',') != std::string::npos;
    if (local_name.find_first_of(',') != std::string::npos) {
      std::stringstream msg;
      msg << "Cannot have comma in card name at line " << line_num;
      fatal(msg);
    }

    // TODO exclude : in strings
    const bool has_colon = (card_value.find_first_of(':') != std::string::npos) ||
                           (local_name.find_first_of(':') != std::string::npos);
    if (!has_colon && ((!lhs_vec && !rhs_vec) || (lhs_vec && !rhs_vec) ||
                       ((lhs_vec || rhs_vec) && !has_comma))) {
      // a = 2
      // a[0] = 2
      // a = b[0]
      std::string expr = global_name + " = " + card_value;
      // add the local card to the locals table
      if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
        std::stringstream msg;
        msg << "Failed to compile expression '" << expr << "' at line " << line_num;
        fatal(msg);
      }
      auto value = vm.globals[global_name.c_str()];
      locations[global_name.c_str()] = line_num;
      comments[global_name.c_str()] = comment;
      comment.clear();
      // Stash the local for this suit
      locals[local_name.c_str()] = value;
    } else if (!lhs_vec && rhs_vec) {
      // a = [1,2,3]
      // loop through comma separated values
      auto open_bracket = card_value.find_first_of('[');
      if (open_bracket != std::string::npos) {
        auto close_bracket = card_value.find_first_of(']', open_bracket);
        card_value =
            card_value.substr(open_bracket + 1, close_bracket - open_bracket - 1);
      }

      // Split card_value by commas, but ignore commas inside quotes
      std::vector<std::string> values;
      std::string current;
      bool in_quotes = false;
      for (size_t i = 0; i < card_value.size(); ++i) {
        char c = card_value[i];
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
        std::string expr = vec_name + " = " + value;
        if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
          std::stringstream msg;
          msg << "Failed to compile expression '" << expr << "' at line " << line_num;
          fatal(msg);
        }
        auto vec_value = vm.globals[vec_name.c_str()];
        locations[vec_name.c_str()] = line_num;
        comments[vec_name.c_str()] = comment;
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
      auto card_values = SplitString(card_value, line_num);
      auto card_names = SplitString(local_name, line_num, card_values.size());

      // the RHS is split up
      // Now create each expression and evaluate it

      if (card_names.size() > card_values.size()) {
        std::stringstream msg;
        msg << "More card names than values at line " << line_num;
        fatal(msg);
      }
      for (int idx = 0; idx < card_names.size(); idx++) {
        std::string local_vec_name = card_names[idx];
        std::string global_vec_name = name_prefix + local_vec_name;

        std::string expr = global_vec_name + " = " + card_values[idx];
        if (vm.interpret(expr.c_str(), '\n', locals) != pips::InterpretResult::OK) {
          std::stringstream msg;
          msg << "Failed to compile expression '" << expr << "' at line " << line_num;
          fatal(msg);
        }
        auto value = vm.globals[global_vec_name.c_str()];
        locations[global_vec_name.c_str()] = line_num;
        comments[global_vec_name.c_str()] = comment;
        comment.clear();
        // Stash the local for this suit
        locals[local_vec_name.c_str()] = value;
      }
    }

  } // end while
}

void Deck::Build(std::istream &ss) {

  // TODO:
  // Support include statements


  // TODO:
  //   combine comment and location into a metadata struct 
  std::map<std::string, int> locations;
  std::map<std::string, std::string> comments;

  if (!deck.empty()) {
    for (const auto &suit : deck) {
      for (const auto &card : suit.second) {
        // replace suit name / with _
        std::string suit_name = suit.first;
        if (suit_name == "/") {
          vm.globals[card.first] = card.second.GetValue();
          locations[card.first] = card.second.loc;
          comments[card.first] = card.second.GetComment();
        } else {
          std::replace(suit_name.begin(), suit_name.end(), '/', '_');
          // use suit name as prefix
          vm.globals[suit_name + "." + card.first] = card.second.GetValue();
          locations[suit_name + "." + card.first] = card.second.loc;
          comments[suit_name + "." + card.first] = card.second.GetComment();
        }
      }
    }
  }
  CompileInput(ss, locations, comments);

  

  for (auto global : vm.globals) {
    const int loc = locations[global.first];
    const auto comment = comments[global.first];
    // find position of the last dot
    const auto last_dot = global.first.find_last_of('.');
    std::string suit, card_name;
    if (last_dot == std::string::npos) {
      suit = "/";
      card_name = global.first;
    } else {
      suit = global.first.substr(0, last_dot);
      card_name = global.first.substr(last_dot + 1);
      std::replace(suit.begin(), suit.end(), '.', '/');
    }
    CopyCard(Card(suit, card_name, global.second, comment, loc));
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
  for (auto global : vm.globals) {
    const auto last_dot = global.first.find_last_of('.');
    std::string suit, card_name;
    if (last_dot == std::string::npos) {
      suit = "/";
      card_name = global.first;
    } else {
      suit = global.first.substr(0, last_dot);
      card_name = global.first.substr(last_dot + 1);
      std::replace(suit.begin(), suit.end(), '.', '/');
    }
    UpdateCard(suit, card_name, global.second);
  }
  return;
}

Card &Deck::GetCard(const std::string &suit, const std::string &name) {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) {
    std::stringstream msg;
    msg << "Suit '" << suit << "' not found in the deck.";
    fatal(msg);
  }
  auto &card = deck[suit][name];
  if (card.empty()) {
    std::stringstream msg;
    msg << "Card '" << name << "' not found in suit '" << suit << "'.";
    fatal(msg);
  }
  return card;
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

void Deck::UpdateCard(const std::string &suit, const std::string &name,
                      const Card &card, std::string comment) {
  auto &mycard = GetCard(suit, name);
  mycard = card;
  if (!comment.empty() && (comment != "")) {
    mycard.UpdateComment(comment);
  } 
}
// functions to iterate over the deck
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
    // replace * with .*
    suit_.replace(suit_.find('*'), 1, ""); 
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
  return suit_it->second.find(name) != suit_it->second.end();
}
void Deck::WriteDeck(std::ostream &os) const {
  for (const auto &suit_name : suits) {
    if (deck.find(suit_name) == deck.end()) continue;
    if (!( suit_name.empty() || (suit_name == "/"))) {
      os << "<" << suit_name << ">\n";
    }
    const auto &suit = deck.at(suit_name);
    for (const auto &card_pair : suit) {
      const auto &card = card_pair.second;
      const std::string name = card_pair.first;
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
