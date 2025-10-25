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

#ifndef RUMMY_DECK_HPP_
#define RUMMY_DECK_HPP_

#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "rummy_utils.hpp"
#include <pips/value_types.hpp>
#include <pips/vm.hpp>

namespace Rummy {

class Card {
 public:
  int loc;
  std::string suit;
  std::string name;
  std::string comment;
  Card() = default;

  Card(const Card &other)
      : loc(other.loc), name(other.name), suit(other.suit), value(other.value), comment(other.comment),
        initialized(true) {}
  Card &operator=(const Card &other) {
    if (this != &other) {
      value = other.value;
      loc = other.loc;
      name = other.name;
      suit = other.suit;
      comment = other.comment;
      initialized = other.initialized;
    }
    return *this;
  }
  template <typename T>
  Card(std::string suit, std::string name, const T &v, std::string comment, int loc = -1)
      : loc(loc), suit(suit), name(name), value(pips::Value(v)), comment(comment), initialized(true) {}

  bool empty() const { return !initialized; }
  bool isBool() const { return value.type == pips::ValueType::BOOL; }
  bool isNumber() const { return value.type == pips::ValueType::NUMBER; }
  bool isString() const { return value.type == pips::ValueType::STRING; }
  pips::Value GetValue() const { return value; }
  std::string GetComment() const { return comment; }
  void UpdateComment(const std::string &new_comment) { comment = new_comment; }

  std::string GetString(int precision = std::numeric_limits<double>::max_digits10) const {
    if (value.type == pips::ValueType::STRING) {
      return std::string(value.as.str);
    } else if (value.type == pips::ValueType::NUMBER) {
      // int or double
      if (static_cast<int>(value.as.number) == value.as.number) {
        return std::to_string(static_cast<int>(value.as.number));
      } else {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(precision) << value.as.number;
        return oss.str();
      }
    } else if (value.type == pips::ValueType::BOOL) {
      return value.as.boolean ? "true" : "false";
    }
    fatal("Value type is not supported for GetString()");
    return "";
  }
  template <typename T>
  T Get() const {
    if constexpr (std::is_same_v<T, std::string>) {
      if (value.type == pips::ValueType::STRING) {
        return std::string(value.as.str);
      }
      std::stringstream msg;
      msg << "Calling Get with a string type but value is not a string at " << suit << "/"
          << name;
      fatal(msg);
    } else if constexpr (std::is_same_v<T, bool>) {
      if (value.type == pips::ValueType::BOOL) {
        return value.as.boolean;
      } else if (value.type == pips::ValueType::NUMBER) {
        return static_cast<bool>(value.as.number);
      }
      std::stringstream msg;
      msg << "Calling Get with a boolean type but value is not a boolean at " << suit
          << "/" << name;
      fatal(msg);

    } else if constexpr (std::is_arithmetic_v<T>) {
      if (value.type == pips::ValueType::NUMBER) {
        return static_cast<T>(value.as.number);
      } else if (std::is_integral_v<T> && (value.type == pips::ValueType::BOOL)){
        return static_cast<T>(value.as.boolean);
      }
      std::stringstream msg;
      msg << "Calling Get with an arithmetic type but value is not a number at " << suit
          << "/" << name;
      fatal(msg);
    }

    return T();
  }

 private:
  pips::Value value;
  bool initialized;
};

class Deck {
 public:
  Deck() = default;
  Deck(const Deck &other) : deck(other.deck) {}
  Deck &operator=(const Deck &other) {
    if (this != &other) {
      deck = other.deck;
    }
    return *this;
  }
  void Build(std::string fname, std::string prepends = "");
  void Build(std::istream &ss);
  void Build(std::istream &ss, std::string prepends);
  void Build(std::istream &ss, std::istream &prepends);
  void CompileInput(std::istream &ss, std::map<std::string, int> &locations,
                    std::map<std::string, std::string> &comments);

  const std::map<std::string, Card> &GetSuit(const std::string &suit) const {
    return deck.at(suit);
  }
  const std::map<std::string, std::map<std::string, Card>> &GetDeck() const {
    return deck;
  }

  template <typename T>
  void AddCard(const std::string &suit, const std::string &name, const T &val, std::string comment = "") {
    if (deck.find(suit) == deck.end()) {
      deck[suit] = std::map<std::string, Card>();
    }
    if constexpr (std::is_same_v<T, Card>) {
      deck[suit][name] = val;
    } else {
      deck[suit][name] = Card(suit, name, val, comment);
    }
  }
  void RemoveCard(const std::string &suit, const std::string &name);
  void CopyCard(const Card &card) { AddCard(card.suit, card.name, card); }
  Card &GetCard(const std::string &suit, const std::string &name);
  template <typename T>
  T GetCardValue(const std::string &suit, const std::string &name) {
    return GetCard(suit, name).Get<T>();
  }

  void UpdateCard(const std::string &suit, const std::string &name, const Card &card, std::string comment="");
  template <typename T>
  void UpdateCard(const std::string &suit, const std::string &name, const T &val, std::string comment="") {
    auto &mycard = GetCard(suit, name);
    if (comment.empty()) {
      comment = mycard.GetComment();
    }
    mycard = Card(suit, name, val, comment, mycard.loc);
  }
  template <typename T>
  T GetOrAddCardValue(const std::string &suit, const std::string &name, const T &val, std::string comment="Default value added at run time") {
    // Like AddCard, but don't error
    if (deck.find(suit) == deck.end()) {
      deck[suit] = std::map<std::string, Card>();
      AddCard<T>(suit,name,val, comment);
      return val;
    }
    const auto it = deck[suit].find(name);
    if ( it == deck[suit].end()) {
      if constexpr (std::is_same_v<T, Card>) {
        deck[suit][name] = val;
      } else {
        deck[suit][name] = Card(suit, name, val, comment);
      }
      return val;
    }
    return GetCard(suit, name).Get<T>();
  }

  // functions to iterate over the deck
  // FindSuit returns a map of cards that match the suit
  std::map<std::string, Card> FindSuit(const std::string &suit) const;
  // fuzzy match version of FindSuit
  std::vector<Card> FindSuitFuzzy(std::string suit_) const;
  std::vector<Card> FindSuitInOrder(const std::string &suit) const;
  std::vector<Card> FindCardFuzzy(std::string suit, std::string name) const;
  bool DoesSuitExist(const std::string &suit) const;
  bool DoesCardExist(const std::string &suit, const std::string &name) const;
  std::vector<std::string> GetSuitsInOrder() const { return suits; }
  
  template <typename T>
  std::vector<T> GetVector(const std::string &suit, const std::string &name) const {
    // Deck stores vectors as separate cards with names of suit.name[index]
    std::string vname = name + "[";
    auto cards = FindCardFuzzy(suit, vname);
    std::vector<T> vec;
    vec.reserve(cards.size());
    for (size_t i = 0; i < cards.size(); i++) {
      vec.push_back(cards[i].Get<T>());
    }
    return vec;
  }
  template <typename T>
  void UpdateVector(const std::string &suit, const std::string &name,
                    const std::vector<T> &values, const std::string comment="") {
    // Deck stores vectors as separate cards with names of suit.name[index]
    for (size_t i = 0; i < values.size(); i++) {
      std::string card_name = name + "[" + std::to_string(i) + "]";
      UpdateCard(suit, card_name, values[i], comment);
    }
  }
  template <typename T>
  void UpdateVector(const std::string &suit, const std::string &name,
                    const std::initializer_list<T> &values, const std::string comment="") {
    UpdateVector(suit, name, std::vector<T>(values), comment);
  }
  template <typename T>
  void AddVector(const std::string &suit, const std::string &name,
                 const std::vector<T> &values, const std::string comment="") {
    // Deck stores vectors as separate cards with names of suit.name[index]
    for (size_t i = 0; i < values.size(); i++) {
      std::string card_name = name + "[" + std::to_string(i) + "]";
      AddCard(suit, card_name, values[i], comment);
    }
  }
  template <typename T>
  void AddVector(const std::string &suit, const std::string &name,
                 const std::initializer_list<T> &values, const std::string comment="") {
    AddVector(suit, name, std::vector<T>(values), comment);
  }
  void RecompileCard(const std::string &line);
  void UpdateDeck();
  void WriteDeck(std::ostream &os) const;

 private:
  pips::VM vm;
  std::map<std::string, std::map<std::string, Card>> deck;
  std::vector<std::string> suits = {"/"}; // suits in order
};

} // namespace Rummy

#endif // RUMMY_DECK_HPP_
