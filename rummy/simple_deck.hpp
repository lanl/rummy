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

#ifndef RUMMY_SIMPLE_DECK_HPP_
#define RUMMY_SIMPLE_DECK_HPP_

#include <istream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "deck_base.hpp"

namespace Rummy {

// SimpleDeck: line-oriented `.par`-style parser.
//
// Use this for older, flat input decks that consist of `key = value` lines
// grouped under `<suit>` headers (no control flow, no user-declared
// pips classes). Cards are sorted by source location (loc) on output.
class SimpleDeck : public DeckBase {
 public:
  SimpleDeck() = default;
  SimpleDeck(const SimpleDeck &other) : DeckBase(other) {}
  SimpleDeck &operator=(const SimpleDeck &other) {
    DeckBase::operator=(other);
    return *this;
  }
  SimpleDeck(SimpleDeck &&) noexcept = default;
  SimpleDeck &operator=(SimpleDeck &&) noexcept = default;

  void Build(std::string fname, std::string prepends = "") override;
  void Build(std::istream &ss) override;
  void Build(std::istream &ss, std::string prepends) override;
  void Build(std::istream &ss, std::istream &prepends) override;
  void BuildSources(const std::vector<InputSource> &sources) override;
  void CompileInput(std::istream &ss, std::map<std::string, CardMeta> &meta,
                    const std::string &base_dir = "");

  // Cards in source order (loc-based).
  std::vector<Card> FindSuitInOrder(const std::string &suit,
                                    const bool fuzzy = false) const override;

  // SimpleDeck-specific WriteDeck variant that sorts inline by loc with a
  // name tiebreaker so programmatically-added cards (loc == -1) still emit.
  void WriteDeck(std::ostream &os) const override;

  // SimpleDeck-only vector helpers (stored as suit.name[index] cards).
  template <typename T>
  void AddVector(const std::string &suit, const std::string &name,
                 const std::vector<T> &values, const std::string comment = "") {
    for (size_t i = 0; i < values.size(); i++) {
      std::string card_name = name + "[" + std::to_string(i) + "]";
      AddCard(suit, card_name, values[i], comment);
    }
  }
  template <typename T>
  void AddVector(const std::string &suit, const std::string &name,
                 const std::initializer_list<T> &values,
                 const std::string comment = "") {
    AddVector(suit, name, std::vector<T>(values), comment);
  }
  template <typename T>
  void UpdateVector(const std::string &suit, const std::string &name,
                    const std::vector<T> &values,
                    const std::string comment = "") {
    for (size_t i = 0; i < values.size(); i++) {
      std::string card_name = name + "[" + std::to_string(i) + "]";
      UpdateCard(suit, card_name, values[i], comment);
    }
  }
  template <typename T>
  void UpdateVector(const std::string &suit, const std::string &name,
                    const std::initializer_list<T> &values,
                    const std::string comment = "") {
    UpdateVector(suit, name, std::vector<T>(values), comment);
  }

  void RecompileCard(const std::string &line);
  void UpdateDeck();

 private:
  void BuildInternal(std::istream &ss, const std::string &base_dir);
  void CompileStream(std::istream &ss, std::map<std::string, CardMeta> &meta,
                     const std::string &base_dir,
                     std::set<std::string> &include_stack, pips::VTable &locals,
                     std::string &curr_suit, std::string &prev_suit);
};

} // namespace Rummy

#endif // RUMMY_SIMPLE_DECK_HPP_
