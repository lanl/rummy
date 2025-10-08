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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "deck.hpp"
#include <sstream>

#define FLOAT_REQUIRE(a, b) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 1e-16))
#define FLOAT_REQUIRE_TOL(a, b, tol) REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, tol))

TEST_CASE("Deck") {
  GIVEN("A simple deck") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "global1 = 42\n"
       << "<suit1>\n"
       << "card1 = global1\n"
       << "card2 = 3.14\n"
       << "<suit2>\n"
       << "card3 = 2 * suit1.card1\n"
       << "card4 = 2.718\n";
    deck.Build(ss);

    THEN("The deck should have the correct number of suits and cards") {
      REQUIRE(deck.GetDeck().size() == 3);
      REQUIRE(deck.GetSuit("suit1").size() == 2);
      REQUIRE(deck.GetSuit("suit2").size() == 2);
    }

    WHEN("We add a new card") {
      deck.AddCard("suit1", "card5", 7.77);
      THEN("The new card should be added correctly") {
        auto card = deck.GetCard("suit1", "card5");
        REQUIRE(card.suit == "suit1");
        REQUIRE(card.name == "card5");
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card5"), 7.77);
      }
    }

    WHEN("We update an existing card") {
      deck.UpdateCard("suit1", "card1", 84);
      THEN("The card should be updated correctly") {
        auto card = deck.GetCard("suit1", "card1");
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card1"), 84);
      }
    }

    WHEN("We retrieve a card value") {
      auto value = deck.GetCardValue<double>("suit2", "card3");
      THEN("The value should be correct") { FLOAT_REQUIRE(value, 84); }
    }

    WHEN("We find a suit") {
      auto suit = deck.FindSuit("suit1");
      THEN("The suit should be found correctly") {
        REQUIRE(suit.size() == 2);
        REQUIRE(suit.find("card1") != suit.end());
        REQUIRE(suit.find("card2") != suit.end());
      }
    }

    WHEN("We add a new suit") {
      deck.AddCard("suit3", "newcard", 999.0);
      THEN("The new suit should be created") {
        REQUIRE(deck.GetSuit("suit3").size() == 1);
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit3", "newcard"), 999.0);
      }
    }
  }
}

TEST_CASE("Deck - String Values") {
  GIVEN("A deck with string values") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "global_str = \"hello\"\n"
       << "<suit1>\n"
       << "card1 = global_str\n"
       << "card2 = \"world\"\n";
    deck.Build(ss);

    WHEN("We retrieve string values") {
      auto value1 = deck.GetCardValue<std::string>("suit1", "card1");
      auto value2 = deck.GetCardValue<std::string>("suit1", "card2");

      THEN("The string values should be correct") {
        REQUIRE(value1 == "hello");
        REQUIRE(value2 == "world");
      }
    }

    WHEN("We add a string card") {
      deck.AddCard("suit1", "card3", std::string("test"));
      THEN("The string card should be added correctly") {
        auto value = deck.GetCardValue<std::string>("suit1", "card3");
        REQUIRE(value == "test");
      }
    }
  }
}

TEST_CASE("Deck - Boolean Values") {
  GIVEN("A deck with boolean values") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "global_bool = true\n"
       << "<suit1>\n"
       << "card1 = global_bool\n"
       << "card2 = false\n";
    deck.Build(ss);

    WHEN("We retrieve boolean values") {
      auto value1 = deck.GetCardValue<bool>("suit1", "card1");
      auto value2 = deck.GetCardValue<bool>("suit1", "card2");

      THEN("The boolean values should be correct") {
        REQUIRE(value1 == true);
        REQUIRE(value2 == false);
      }
    }

    WHEN("We add a boolean card") {
      deck.AddCard("suit1", "card3", true);
      THEN("The boolean card should be added correctly") {
        auto value = deck.GetCardValue<bool>("suit1", "card3");
        REQUIRE(value == true);
      }
    }
  }
}

TEST_CASE("Deck - Complex Expressions") {
  GIVEN("A deck with complex expressions") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "global1 = 10\n"
       << "global2 = 20\n"
       << "<suit1>\n"
       << "card1 = global1 + global2\n"
       << "card2 = global1 * 2\n"
       << "card3 = global1 / 2\n"
       << "card4 = global2 - global1\n";
    deck.Build(ss);

    WHEN("We retrieve calculated values") {
      auto value1 = deck.GetCardValue<double>("suit1", "card1");
      auto value2 = deck.GetCardValue<double>("suit1", "card2");
      auto value3 = deck.GetCardValue<double>("suit1", "card3");
      auto value4 = deck.GetCardValue<double>("suit1", "card4");

      THEN("The calculated values should be correct") {
        FLOAT_REQUIRE(value1, 30.0);
        FLOAT_REQUIRE(value2, 20.0);
        FLOAT_REQUIRE(value3, 5.0);
        FLOAT_REQUIRE(value4, 10.0);
      }
    }
  }
}

TEST_CASE("Deck - Vector Operations") {
  GIVEN("A deck with vector operations") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "global_vec = [1, 2, 3]\n"
       << "<suit1>\n"
       << "card1[:] = global_vec[:3]\n";
    deck.Build(ss);

    WHEN("We retrieve vector values") {
      auto vector_val = deck.GetVector<double>("suit1", "card1");

      THEN("The vector should be correct") {
        REQUIRE(vector_val.size() == 3);
        FLOAT_REQUIRE(vector_val[0], 1.0);
        FLOAT_REQUIRE(vector_val[1], 2.0);
        FLOAT_REQUIRE(vector_val[2], 3.0);
      }
    }

    WHEN("We add a vector") {
      deck.AddVector("suit1", "card2", std::vector<double>{4.0, 5.0, 6.0});

      THEN("The vector should be added correctly") {
        auto vector_val = deck.GetVector<double>("suit1", "card2");
        REQUIRE(vector_val.size() == 3);
        FLOAT_REQUIRE(vector_val[0], 4.0);
        FLOAT_REQUIRE(vector_val[1], 5.0);
        FLOAT_REQUIRE(vector_val[2], 6.0);
      }
    }

    WHEN("We update a vector") {
      deck.UpdateVector("suit1", "card1", std::vector<double>{7.0, 8.0});

      THEN("The vector should be updated correctly") {
        auto vector_val = deck.GetVector<double>("suit1", "card1");
        REQUIRE(vector_val.size() == 3); // only overides the first two elements
        FLOAT_REQUIRE(vector_val[0], 7.0);
        FLOAT_REQUIRE(vector_val[1], 8.0);
        FLOAT_REQUIRE(vector_val[2], 3.0);
      }
    }
  }
}

TEST_CASE("Deck - Build with String Parameter") {
  GIVEN("A deck built with string parameter") {
    Rummy::Deck deck;
    std::string content = "global1 = 42\n<suit1>\ncard1 = global1\n";
    std::stringstream ss(content);

    WHEN("We build the deck with string") {
      deck.Build(ss);

      THEN("The deck should be built correctly") {
        REQUIRE(deck.GetDeck().size() == 2); // "/" and "suit1"
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card1"), 42.0);
      }
    }
  }
}

TEST_CASE("Deck - Build with Additional Configuration") {
  GIVEN("A deck with additional configuration") {
    Rummy::Deck deck;
    std::string content = "global1 = c * 2\n<suit1>\ncard1 = global1\n";
    std::string config = "c = 3e8\n";
    std::stringstream ss(content);

    WHEN("We build the deck with configuration") {
      deck.Build(ss, config);

      THEN("The deck should use the configuration") {
        REQUIRE(deck.GetDeck().size() == 2);
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card1"), 6e8);
      }
    }
  }
}

TEST_CASE("Card - Constructor and Methods") {
  GIVEN("A card object") {
    Rummy::Card card("hearts", "ace", 1.0, 5);

    WHEN("We access card properties") {
      THEN("The properties should be correct") {
        REQUIRE(card.suit == "hearts");
        REQUIRE(card.name == "ace");
        REQUIRE(card.loc == 5);
        FLOAT_REQUIRE(card.Get<int>(), 1);
        REQUIRE(card.GetString() == "1");
      }
    }

    WHEN("We copy the card") {
      Rummy::Card card2 = card;

      THEN("The copy should be identical") {
        REQUIRE(card2.suit == "hearts");
        REQUIRE(card2.name == "ace");
        REQUIRE(card2.loc == 5);
        FLOAT_REQUIRE(card2.Get<double>(), 1.0);
      }
    }

    WHEN("We assign the card") {
      Rummy::Card card3;
      card3 = card;

      THEN("The assignment should work correctly") {
        REQUIRE(card3.suit == "hearts");
        REQUIRE(card3.name == "ace");
        REQUIRE(card3.loc == 5);
        FLOAT_REQUIRE(card3.Get<double>(), 1.0);
      }
    }
  }
}

TEST_CASE("Card - Different Value Types") {
  GIVEN("Cards with different value types") {
    Rummy::Card string_card("spades", "king", std::string("face"));
    Rummy::Card bool_card("clubs", "joker", true);
    Rummy::Card int_card("diamonds", "ten", 10);

    WHEN("We get string representations") {
      THEN("Each type should convert correctly") {
        REQUIRE(string_card.GetString() == "face");
        REQUIRE(bool_card.GetString() == "true");
        REQUIRE(int_card.GetString() == "10");
      }
    }

    WHEN("We get typed values") {
      THEN("Each type should return correctly") {
        REQUIRE(string_card.Get<std::string>() == "face");
        REQUIRE(bool_card.Get<bool>() == true);
        REQUIRE(int_card.Get<int>() == 10);
      }
    }
  }
}

TEST_CASE("Deck - Edge Cases") {
  GIVEN("A deck for edge case testing") {
    Rummy::Deck deck;

    WHEN("We build an empty deck") {
      std::stringstream empty_ss;
      deck.Build(empty_ss);

      THEN("We can add cards to it") {
        deck.AddCard("empty_suit", "empty_card", 0);
        auto card = deck.GetCard("empty_suit", "empty_card");
        REQUIRE(card.suit == "empty_suit");
        REQUIRE(card.name == "empty_card");
        FLOAT_REQUIRE(card.Get<int>(), 0);
      }
    }
  }
}
