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

#include "deck_base.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "rummy_utils.hpp"
#include <pips/vm.hpp>

namespace Rummy {

// ---------------------------------------------------------------------------
// Card / Suit accessors
// ---------------------------------------------------------------------------
Card &DeckBase::GetCard(const std::string &suit, const std::string &name) {
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

void DeckBase::RemoveCard(const std::string &suit, const std::string &name) {
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

void DeckBase::UpdateCard(const std::string &suit, const std::string &name,
                          const Card &card, std::string comment) {
  auto &mycard = GetCard(suit, name);
  mycard = card;
  if (!comment.empty()) mycard.UpdateComment(comment);
}

std::vector<std::string>
DeckBase::GetCardsInOrder(const std::string &suit) const {
  auto it = card_map.find(suit);
  if (it != card_map.end()) return it->second;
  return {};
}

std::map<std::string, Card> DeckBase::FindSuit(const std::string &suit) const {
  auto it = deck.find(suit);
  if (it == deck.end()) {
    std::stringstream msg;
    msg << "Suit '" << suit << "' not found in the deck.";
    fatal(msg);
  }
  return it->second;
}

std::vector<Card> DeckBase::FindSuitFuzzy(std::string suit_) const {
  if (suit_ != "/") {
    auto star_pos = suit_.find('*');
    if (star_pos != std::string::npos) suit_.erase(star_pos, 1);
  }
  std::vector<Card> result;
  for (const auto &[suit_name, cards] : deck) {
    if (suit_name.find(suit_) != std::string::npos) {
      for (const auto &[_, card] : cards) result.push_back(card);
    }
  }
  return result;
}

std::vector<Card> DeckBase::FindCardFuzzy(std::string suit,
                                          std::string name) const {
  std::vector<Card> result;
  auto cards = FindSuit(suit);
  for (const auto &[card_name, card] : cards) {
    if (card_name.find(name) != std::string::npos) result.push_back(card);
  }
  return result;
}

bool DeckBase::DoesSuitExist(const std::string &suit) const {
  return deck.find(suit) != deck.end();
}

bool DeckBase::DoesCardExist(const std::string &suit,
                             const std::string &name) const {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) return false;
  return suit_it->second.count(name) > 0 ||
         suit_it->second.count(name + "[0]") > 0;
}

bool DeckBase::IsCardVector(const std::string &suit,
                            const std::string &name) const {
  auto suit_it = deck.find(suit);
  if (suit_it == deck.end()) return false;
  // Check for a native vector card stored as a single entry.
  auto card_it = suit_it->second.find(name);
  if (card_it != suit_it->second.end() &&
      card_it->second.GetValue().type == pips::ValueType::VECTOR) {
    return true;
  }
  // Check for per-element legacy cards (name[0], name[1], ...).
  for (const auto &[card_name, _] : suit_it->second) {
    if (card_name == name + "[0]") return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Default WriteDeck: <suit> headers + cards in FindSuitInOrder order.
// ---------------------------------------------------------------------------
void DeckBase::WriteDeck(std::ostream &os) const {
  for (const auto &sp : suits) {
    auto sit = deck.find(sp);
    if (sit == deck.end()) continue;
    if (!(sp.empty() || sp == "/")) os << "<" << sp << ">\n";
    auto cards = FindSuitInOrder(sp);
    for (const auto &c : cards) {
      os << c.name << " = ";
      if (c.isString()) os << "\"" << c.GetString() << "\"";
      else os << c.GetString();
      if (!c.GetComment().empty()) os << "  # " << c.GetComment();
      os << "\n";
    }
    os << "\n";
  }
}

// ---------------------------------------------------------------------------
// CopyVmStateImpl: deep-copy VM functions/classes/instances/globals into `vm`.
// ---------------------------------------------------------------------------
void DeckBase::CopyVmStateImpl(const pips::VM &other_vm) {
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

  auto copy_value = [&](const pips::Value &source_value) {
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
    if (!source_instance || !copied_instance) continue;
    for (const auto &[field_name, field_value] : source_instance->fields) {
      copied_instance->fields[field_name] = copy_value(field_value);
    }
  }

  for (const auto &[name, value] : other_vm.globals) {
    vm.globals[name] = copy_value(value);
  }
}

} // namespace Rummy
