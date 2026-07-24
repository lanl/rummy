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

#ifndef RUMMY_DECK_BASE_HPP_
#define RUMMY_DECK_BASE_HPP_

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "rummy_utils.hpp"
#include <pips/value_types.hpp>
#include <pips/vm.hpp>

namespace Rummy {

// A named deck source. Supplying the base directory separately lets callers
// compose a program from several files without changing the meaning of
// relative include directives in each file.
struct InputSource {
  std::string name;
  std::string contents;
  std::string base_dir;
};

// ---------------------------------------------------------------------------
// Card: a single named, typed value with an optional comment and source loc.
// ---------------------------------------------------------------------------
class Card {
 public:
  int loc;
  std::string suit;
  std::string name;
  std::string comment;

  Card() : initialized(false) {}

  Card(const Card &other)
      : loc(other.loc), suit(other.suit), name(other.name),
        comment(other.comment), initialized(other.initialized) {
    CopyValueFrom(other.value);
  }
  Card &operator=(const Card &other) {
    if (this != &other) {
      loc = other.loc;
      name = other.name;
      suit = other.suit;
      comment = other.comment;
      initialized = other.initialized;
      CopyValueFrom(other.value);
    }
    return *this;
  }

  template <typename T>
  Card(std::string suit, std::string name, const T &v, std::string comment,
       int loc = -1)
      : loc(loc), suit(suit), name(name), comment(comment), initialized(true) {
    if constexpr (std::is_same_v<T, std::string>) {
      AdoptString(v);
    } else if constexpr (std::is_same_v<T, const char *> ||
                         std::is_same_v<T, char *>) {
      AdoptString(std::string(v));
    } else if constexpr (std::is_same_v<T, pips::Value>) {
      AdoptValue(v);
    } else {
      value = pips::Value(v);
    }
  }
  Card(std::string suit, std::string name, pips::Value v, std::string comment,
       int loc = -1)
      : loc(loc), suit(suit), name(name), comment(comment), initialized(true) {
    AdoptValue(v);
  }

  bool empty() const { return !initialized; }
  bool isBool() const { return value.type == pips::ValueType::BOOL; }
  bool isNumber() const { return value.type == pips::ValueType::NUMBER; }
  bool isString() const { return value.type == pips::ValueType::STRING; }
  pips::Value GetValue() const { return value; }
  std::string GetComment() const { return comment; }
  void UpdateComment(const std::string &new_comment) { comment = new_comment; }

  std::string
  GetString(int precision = std::numeric_limits<double>::max_digits10) const {
    if (value.type == pips::ValueType::STRING) {
      return value.as.string ? value.as.string->str : std::string();
    } else if (value.type == pips::ValueType::NUMBER) {
      if (static_cast<int>(value.as.number) == value.as.number) {
        return std::to_string(static_cast<int>(value.as.number));
      } else {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(precision)
            << value.as.number;
        return oss.str();
      }
    } else if (value.type == pips::ValueType::BOOL) {
      return value.as.boolean ? "true" : "false";
    } else if (value.type == pips::ValueType::VECTOR && value.as.vector) {
      std::string result = "[";
      for (size_t i = 0; i < value.as.vector->elements.size(); ++i) {
        if (i > 0) result += ", ";
        const pips::Value &elem = value.as.vector->elements[i];
        if (elem.type == pips::ValueType::NUMBER) {
          if (static_cast<int>(elem.as.number) == elem.as.number) {
            result += std::to_string(static_cast<int>(elem.as.number));
          } else {
            std::ostringstream oss;
            oss << std::scientific << std::setprecision(precision)
                << elem.as.number;
            result += oss.str();
          }
        } else if (elem.type == pips::ValueType::BOOL) {
          result += elem.as.boolean ? "true" : "false";
        } else if (elem.type == pips::ValueType::STRING && elem.as.string) {
          result += "\"" + elem.as.string->str + "\"";
        }
      }
      result += "]";
      return result;
    }
    fatal("Value type is not supported for GetString()");
    return "";
  }

  template <typename T>
  T Get() const {
    if constexpr (std::is_same_v<T, std::string>) {
      if (value.type == pips::ValueType::STRING) {
        return value.as.string ? value.as.string->str : std::string();
      }
      std::stringstream msg;
      msg << "Calling Get with a string type but value is not a string at "
          << suit << "/" << name;
      fatal(msg);
    } else if constexpr (std::is_same_v<T, bool>) {
      if (value.type == pips::ValueType::BOOL) return value.as.boolean;
      if (value.type == pips::ValueType::NUMBER)
        return static_cast<bool>(value.as.number);
      std::stringstream msg;
      msg << "Calling Get with a boolean type but value is not a boolean at "
          << suit << "/" << name;
      fatal(msg);
    } else if constexpr (std::is_arithmetic_v<T>) {
      if (value.type == pips::ValueType::NUMBER)
        return static_cast<T>(value.as.number);
      if (std::is_integral_v<T> && value.type == pips::ValueType::BOOL)
        return static_cast<T>(value.as.boolean);
      std::stringstream msg;
      msg << "Calling Get with an arithmetic type but value is not a number at "
          << suit << "/" << name;
      fatal(msg);
    }
    return T();
  }

 private:
  void AdoptValue(const pips::Value &v) {
    value = v;
    if (v.type == pips::ValueType::STRING && v.as.string) {
      AdoptString(v.as.string->str);
    } else {
      owned_string_.reset();
    }
  }
  void AdoptString(const std::string &s) {
    owned_string_ = std::make_unique<pips::StringObject>();
    owned_string_->str = s;
    value.type = pips::ValueType::STRING;
    value.as.string = owned_string_.get();
  }
  void CopyValueFrom(const pips::Value &v) { AdoptValue(v); }

  pips::Value value;
  std::unique_ptr<pips::StringObject> owned_string_;
  bool initialized;
};

struct CardMeta {
  int loc = -1;
  std::string comment;
};

// ---------------------------------------------------------------------------
// DeckBase: shared storage and lookup machinery for every Rummy deck flavor.
//
// Derived classes own the parsing strategy (Build*) and any specialized
// readback / lowering logic. DeckBase owns the in-memory representation
// (suits/cards/card_map) and the pips::VM used for evaluation.
// ---------------------------------------------------------------------------
class DeckBase {
 public:
  DeckBase() = default;
  virtual ~DeckBase() = default;
  DeckBase(const DeckBase &other)
      : deck(other.deck), suits(other.suits), card_map(other.card_map) {
    CopyVmStateImpl(other.vm);
  }
  DeckBase &operator=(const DeckBase &other) {
    if (this != &other) {
      deck = other.deck;
      suits = other.suits;
      card_map = other.card_map;
      vm = pips::VM();
      CopyVmStateImpl(other.vm);
    }
    return *this;
  }
  DeckBase(DeckBase &&) noexcept = default;
  DeckBase &operator=(DeckBase &&) noexcept = default;

  // Parsing — derived classes choose the strategy.
  virtual void Build(std::string fname, std::string prepends = "") = 0;
  virtual void Build(std::istream &ss) = 0;
  virtual void Build(std::istream &ss, std::string prepends) = 0;
  virtual void Build(std::istream &ss, std::istream &prepends) = 0;
  virtual void BuildSources(const std::vector<InputSource> &sources) = 0;

  // ---- Accessors -------------------------------------------------------
  const std::map<std::string, Card> &GetSuit(const std::string &suit) const {
    return deck.at(suit);
  }
  const std::map<std::string, std::map<std::string, Card>> &GetDeck() const {
    return deck;
  }
  std::vector<std::string> GetSuitsInOrder() const { return suits; }

  // ---- Card mutation ---------------------------------------------------
  template <typename T>
  void AddCard(const std::string &suit, const std::string &name, const T &val,
               std::string comment = "") {
    if (deck.find(suit) == deck.end()) {
      deck[suit] = std::map<std::string, Card>();
      suits.push_back(suit);
      card_map[suit] = std::vector<std::string>();
    }
    if constexpr (std::is_same_v<T, Card>) {
      deck[suit][name] = val;
    } else {
      deck[suit][name] = Card(suit, name, val, comment);
    }
  }
  void CopyCard(const Card &card) { AddCard(card.suit, card.name, card); }

  void RemoveCard(const std::string &suit, const std::string &name);
  Card &GetCard(const std::string &suit, const std::string &name);

  template <typename T>
  T GetCardValue(const std::string &suit, const std::string &name) {
    return GetCard(suit, name).Get<T>();
  }

  void UpdateCard(const std::string &suit, const std::string &name,
                  const Card &card, std::string comment = "");
  template <typename T>
  void UpdateCard(const std::string &suit, const std::string &name,
                  const T &val, std::string comment = "") {
    auto &mycard = GetCard(suit, name);
    if (comment.empty()) comment = mycard.GetComment();
    mycard = Card(suit, name, val, comment, mycard.loc);
  }
  template <typename T>
  T GetOrAddCardValue(const std::string &suit, const std::string &name,
                      const T &val,
                      std::string comment = "Default value added at run time") {
    if (deck.find(suit) == deck.end()) {
      AddCard<T>(suit, name, val, comment);
      return val;
    }
    const auto it = deck[suit].find(name);
    if (it == deck[suit].end()) {
      if constexpr (std::is_same_v<T, Card>) {
        deck[suit][name] = val;
      } else {
        deck[suit][name] = Card(suit, name, val, comment);
      }
      return val;
    }
    return GetCard(suit, name).Get<T>();
  }

  // ---- Lookups & predicates --------------------------------------------
  std::map<std::string, Card> FindSuit(const std::string &suit) const;
  std::vector<Card> FindSuitFuzzy(std::string suit_) const;
  // FindSuitInOrder differs by parser flavor (loc vs. card_map ordering).
  virtual std::vector<Card> FindSuitInOrder(const std::string &suit,
                                            const bool fuzzy = false) const = 0;
  std::vector<Card> FindCardFuzzy(std::string suit, std::string name) const;
  bool DoesSuitExist(const std::string &suit) const;
  bool DoesCardExist(const std::string &suit, const std::string &name) const;
  std::vector<std::string> GetCardsInOrder(const std::string &suit) const;
  bool IsCardVector(const std::string &suit, const std::string &name) const;

  template <typename T>
  std::vector<T> GetVector(const std::string &suit, const std::string &name,
                           std::vector<std::string> &comments) const {
    // Check for a native vector card stored as a single entry.
    auto suit_it = deck.find(suit);
    if (suit_it != deck.end()) {
      auto card_it = suit_it->second.find(name);
      if (card_it != suit_it->second.end()) {
        const pips::Value &val = card_it->second.GetValue();
        if (val.type == pips::ValueType::VECTOR && val.as.vector) {
          std::vector<T> vec;
          const std::string &card_comment = card_it->second.GetComment();
          vec.reserve(val.as.vector->elements.size());
          for (const auto &elem : val.as.vector->elements) {
            Card tmp("", name, elem, "");
            if constexpr (std::is_same_v<T, std::string>) {
              vec.push_back(tmp.GetString());
            } else {
              vec.push_back(tmp.Get<T>());
            }
            comments.push_back(card_comment);
          }
          return vec;
        }
      }
    }
    // Fall back to per-element cards (name[0], name[1], ...).
    std::string vname = name + "[";
    auto cards = FindCardFuzzy(suit, vname);
    std::sort(cards.begin(), cards.end(), [](const Card &a, const Card &b) {
      auto ai = a.name.find_last_of('[');
      auto bi = b.name.find_last_of('[');
      if (ai == std::string::npos || bi == std::string::npos)
        return a.name < b.name;
      return std::stoi(a.name.substr(ai + 1)) <
             std::stoi(b.name.substr(bi + 1));
    });
    std::vector<T> vec;
    vec.reserve(cards.size());
    for (size_t i = 0; i < cards.size(); i++) {
      if constexpr (std::is_same_v<T, std::string>) {
        vec.push_back(cards[i].GetString());
      } else {
        vec.push_back(cards[i].Get<T>());
      }
      comments.push_back(cards[i].GetComment());
    }
    return vec;
  }
  template <typename T>
  std::vector<T> GetVector(const std::string &suit,
                           const std::string &name) const {
    std::vector<std::string> comments;
    return GetVector<T>(suit, name, comments);
  }

  // ---- Output ----------------------------------------------------------
  // Default emits <suit> headers followed by cards in FindSuitInOrder()
  // order. Subclasses may override for custom formatting.
  virtual void WriteDeck(std::ostream &os) const;

  // ---- Seeding ---------------------------------------------------------
  void SeedGlobals(
      const std::map<std::string, std::map<std::string, Card>> &new_cards,
      const std::vector<std::string> &new_suits,
      const std::map<std::string, std::vector<std::string>> &new_card_map) {
    for (const auto &suit : new_suits) {
      if (deck.find(suit) == deck.end()) {
        deck[suit] = {};
        suits.push_back(suit);
        card_map[suit] = {};
      }
    }
    for (const auto &[suit, names] : new_card_map) {
      auto &cm = card_map[suit];
      for (const auto &n : names) {
        if (std::find(cm.begin(), cm.end(), n) == cm.end()) cm.push_back(n);
      }
    }
    for (const auto &[suit, cards] : new_cards) {
      for (const auto &[card_name, card] : cards) {
        deck[suit][card_name] = card;
      }
    }
  }

 protected:
  // Deep-copy another VM's state into `vm`. Used by copy constructors and
  // assignment operators. Marked virtual so derived classes can layer
  // post-copy bookkeeping (e.g. rebuilding an instance registry).
  virtual void CopyVmState(const pips::VM &other_vm) {
    CopyVmStateImpl(other_vm);
  }
  // Non-virtual implementation, safe to call from constructors/assignment.
  void CopyVmStateImpl(const pips::VM &other_vm);

  pips::VM vm;
  std::map<std::string, std::map<std::string, Card>> deck = {{"/", {}}};
  std::vector<std::string> suits = {"/"};
  std::map<std::string, std::vector<std::string>> card_map;
};

} // namespace Rummy

#endif // RUMMY_DECK_BASE_HPP_
