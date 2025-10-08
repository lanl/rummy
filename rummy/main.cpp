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

#include <iostream>
#include <memory>
#include <string>

#include "deck.hpp"

void Deal(Rummy::Deck *deck) {
  std::cout << "Dealing the cards..." << std::endl;
  for (const auto &suit : deck->GetDeck()) {
    std::cout << suit.first << std::endl;
    for (const auto &card : suit.second) {
      printf("%02d: %s = %s\n", card.second.loc, card.first.c_str(),
             card.second.GetString().c_str());
    }
  }
  std::cout << "=====================" << std::endl;
}

std::string DefinePC() {
  // std::string pc = R"(
  //   <physical/constants>
  //    c = 3e8
  //    kb = 1.380649e-23
  // )";
  std::string pc = "c = 3e8";
  return pc;
}

int main(int argc, char *argv[]) {

  if (argc == 2) {
    // auto deck = std::make_unique<Rummy::Deck>();
    Rummy::Deck deck;
    // deck->Build(argv[1]);
    deck.Build(argv[1], DefinePC());

    // const auto &a = deck->GetVector<double>("/", "a");
    // for (const auto &val : a) {
    //   std::cout << val << std::endl;
    // }
    // deck->UpdateVector("/", "a", {0.1, 0.2, 0.3});
    // const auto &a1 = deck->GetVector<double>("/", "a");
    // for (const auto &val : a1) {
    //   std::cout << val << std::endl;
    // }

    // deck->AddVector("<new>", "a", {10.0, 20.0, 30.0});
    // const auto &a2 = deck->GetVector<double>("<new>", "a");
    // for (const auto &val : a2) {
    //   std::cout << val << std::endl;
    // }
    Deal(&deck);
  }
  return 0;
}
