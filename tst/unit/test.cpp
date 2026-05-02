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
      deck.AddCard("suit1", "card5", 7.77, "Test comment for card5");
      THEN("The new card should be added correctly") {
        auto card = deck.GetCard("suit1", "card5");
        REQUIRE(card.suit == "suit1");
        REQUIRE(card.name == "card5");
        REQUIRE(card.comment == "Test comment for card5");
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card5"), 7.77);
      }
    }

    WHEN("We update an existing card") {
      auto original_comment = deck.GetCard("suit1", "card1").comment;
      deck.UpdateCard("suit1", "card1", 84);
      THEN("The card should be updated correctly") {
        auto card = deck.GetCard("suit1", "card1");
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card1"), 84);
        // Comment should be preserved when not explicitly provided
        REQUIRE(card.comment == original_comment);
      }
    }

    WHEN("We update a card with a new comment") {
      deck.UpdateCard("suit1", "card1", 84, "Updated comment");
      THEN("The card should be updated with the new comment") {
        auto card = deck.GetCard("suit1", "card1");
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "card1"), 84);
        REQUIRE(card.comment == "Updated comment");
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
      deck.AddCard("suit3", "newcard", 999.0, "New suit comment");
      THEN("The new suit should be created") {
        REQUIRE(deck.GetSuit("suit3").size() == 1);
        FLOAT_REQUIRE(deck.GetCardValue<double>("suit3", "newcard"), 999.0);
        auto card = deck.GetCard("suit3", "newcard");
        REQUIRE(card.comment == "New suit comment");
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
      deck.AddCard("suit1", "card3", std::string("test"), "String card comment");
      THEN("The string card should be added correctly") {
        auto value = deck.GetCardValue<std::string>("suit1", "card3");
        REQUIRE(value == "test");
        auto card = deck.GetCard("suit1", "card3");
        REQUIRE(card.comment == "String card comment");
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
      deck.AddCard("suit1", "card3", true, "Boolean card comment");
      THEN("The boolean card should be added correctly") {
        auto value = deck.GetCardValue<bool>("suit1", "card3");
        REQUIRE(value == true);
        auto card = deck.GetCard("suit1", "card3");
        REQUIRE(card.comment == "Boolean card comment");
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
      deck.AddVector("suit1", "card2", std::vector<double>{4.0, 5.0, 6.0}, "Vector comment");

      THEN("The vector should be added correctly") {
        auto vector_val = deck.GetVector<double>("suit1", "card2");
        REQUIRE(vector_val.size() == 3);
        FLOAT_REQUIRE(vector_val[0], 4.0);
        FLOAT_REQUIRE(vector_val[1], 5.0);
        FLOAT_REQUIRE(vector_val[2], 6.0);
        // Check comment on first element
        auto card = deck.GetCard("suit1", "card2[0]");
        REQUIRE(card.comment == "Vector comment");
      }
    }

    WHEN("We update a vector") {
      deck.UpdateVector("suit1", "card1", std::vector<double>{7.0, 8.0}, "Updated vector comment");

      THEN("The vector should be updated correctly") {
        auto vector_val = deck.GetVector<double>("suit1", "card1");
        REQUIRE(vector_val.size() == 3); // only overides the first two elements
        FLOAT_REQUIRE(vector_val[0], 7.0);
        FLOAT_REQUIRE(vector_val[1], 8.0);
        FLOAT_REQUIRE(vector_val[2], 3.0);
        // Check comment on updated element
        auto card = deck.GetCard("suit1", "card1[0]");
        REQUIRE(card.comment == "Updated vector comment");
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
    Rummy::Card card("hearts", "ace", 1.0, "hearts ace", 5);

    WHEN("We access card properties") {
      THEN("The properties should be correct") {
        REQUIRE(card.suit == "hearts");
        REQUIRE(card.name == "ace");
        REQUIRE(card.comment == "hearts ace");
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
        REQUIRE(card2.comment == "hearts ace");
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
    Rummy::Card string_card("spades", "king", std::string("face"), "String card", -1);
    Rummy::Card bool_card("clubs", "joker", true, "Boolean card", -1);
    Rummy::Card int_card("diamonds", "ten", 10, "Integer card", -1);

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

    WHEN("We check comments") {
      THEN("Each card should have the correct comment") {
        REQUIRE(string_card.comment == "String card");
        REQUIRE(bool_card.comment == "Boolean card");
        REQUIRE(int_card.comment == "Integer card");
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
        deck.AddCard("empty_suit", "empty_card", 0, "Empty card comment");
        auto card = deck.GetCard("empty_suit", "empty_card");
        REQUIRE(card.suit == "empty_suit");
        REQUIRE(card.name == "empty_card");
        REQUIRE(card.comment == "Empty card comment");
        FLOAT_REQUIRE(card.Get<int>(), 0);
      }
    }
  }
}

TEST_CASE("Card - Comment Operations") {
  GIVEN("A card with a comment") {
    Rummy::Card card("hearts", "ace", 1, "Original comment", 5);

    WHEN("We get the comment") {
      auto comment = card.GetComment();
      THEN("The comment should be correct") {
        REQUIRE(comment == "Original comment");
        REQUIRE(card.comment == "Original comment");
      }
    }

    WHEN("We update the comment") {
      card.UpdateComment("New comment");
      THEN("The comment should be updated") {
        REQUIRE(card.GetComment() == "New comment");
        REQUIRE(card.comment == "New comment");
      }
    }

    WHEN("We update the comment to an empty string") {
      card.UpdateComment("");
      THEN("The comment should be empty") {
        REQUIRE(card.GetComment() == "");
        REQUIRE(card.comment == "");
      }
    }
  }

  GIVEN("Multiple cards with different comments") {
    Rummy::Deck deck;
    deck.AddCard("test", "card1", 10, "Comment 1");
    deck.AddCard("test", "card2", 20, "Comment 2");
    deck.AddCard("test", "card3", 30, "Comment 3");

    WHEN("We retrieve and verify each comment") {
      THEN("Each card should have its own comment") {
        REQUIRE(deck.GetCard("test", "card1").GetComment() == "Comment 1");
        REQUIRE(deck.GetCard("test", "card2").GetComment() == "Comment 2");
        REQUIRE(deck.GetCard("test", "card3").GetComment() == "Comment 3");
      }
    }

    WHEN("We update one card's comment") {
      auto &card = deck.GetCard("test", "card2");
      card.UpdateComment("Modified comment");
      
      THEN("Only that card's comment should change") {
        REQUIRE(deck.GetCard("test", "card1").GetComment() == "Comment 1");
        REQUIRE(deck.GetCard("test", "card2").GetComment() == "Modified comment");
        REQUIRE(deck.GetCard("test", "card3").GetComment() == "Comment 3");
      }
    }
  }
}
TEST_CASE("Deck - Hash inside quoted string value") {
  GIVEN("A deck where a string value contains '#'") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "<suit1>\n"
       << "url = \"http://example.com\"  # real comment\n"
       << "tag = \"hello#world\"\n";
    deck.Build(ss);

    THEN("The string values should be preserved verbatim") {
      REQUIRE(deck.GetCardValue<std::string>("suit1", "url") == "http://example.com");
      REQUIRE(deck.GetCardValue<std::string>("suit1", "tag") == "hello#world");
    }
    THEN("The trailing comment on 'url' should be captured") {
      REQUIRE(deck.GetCard("suit1", "url").GetComment() == "real comment");
    }
  }
}

TEST_CASE("Deck - Deck copy constructor and assignment") {
  GIVEN("A built deck") {
    Rummy::Deck original;
    std::stringstream ss;
    ss << "g = 9\n"
       << "<physics>\n"
       << "energy = 42\n"
       << "<hydro>\n"
       << "cfl = 0.3\n";
    original.Build(ss);

    WHEN("We copy-construct a new deck") {
      Rummy::Deck copy(original);

      THEN("All suits are present in the copy") {
        REQUIRE(copy.DoesSuitExist("/"));
        REQUIRE(copy.DoesSuitExist("physics"));
        REQUIRE(copy.DoesSuitExist("hydro"));
      }
      THEN("Card values are correct in the copy") {
        FLOAT_REQUIRE(copy.GetCardValue<double>("/", "g"), 9.0);
        FLOAT_REQUIRE(copy.GetCardValue<double>("physics", "energy"), 42.0);
        FLOAT_REQUIRE(copy.GetCardValue<double>("hydro", "cfl"), 0.3);
      }
      THEN("Suit order is preserved in the copy") {
        auto suits = copy.GetSuitsInOrder();
        REQUIRE(suits.size() == 3);
        REQUIRE(suits[0] == "/");
        REQUIRE(suits[1] == "physics");
        REQUIRE(suits[2] == "hydro");
      }
    }

    WHEN("We copy-assign a new deck") {
      Rummy::Deck assigned;
      assigned = original;

      THEN("All suits are present in the assigned deck") {
        REQUIRE(assigned.DoesSuitExist("physics"));
        REQUIRE(assigned.DoesSuitExist("hydro"));
      }
      THEN("Suit order is preserved in the assigned deck") {
        auto suits = assigned.GetSuitsInOrder();
        REQUIRE(suits.size() == 3);
        REQUIRE(suits[1] == "physics");
        REQUIRE(suits[2] == "hydro");
      }
    }
  }
}

TEST_CASE("Deck - AddCard adds new suit to ordering") {
    GIVEN("An empty deck") {
    Rummy::Deck deck;

    WHEN("We add cards to a new suit programmatically") {
      deck.AddCard("alpha", "x", 1.0);
      deck.AddCard("beta",  "y", 2.0);

      THEN("Both suits appear in order") {
        auto suits = deck.GetSuitsInOrder();
        REQUIRE(deck.DoesSuitExist("alpha"));
        REQUIRE(deck.DoesSuitExist("beta"));
        // '/' is always first; alpha and beta should follow
        REQUIRE(std::find(suits.begin(), suits.end(), "alpha") != suits.end());
        REQUIRE(std::find(suits.begin(), suits.end(), "beta")  != suits.end());
      }

      THEN("WriteDeck includes both suits") {
        std::ostringstream out;
        deck.WriteDeck(out);
        REQUIRE_THAT(out.str(), Catch::Matchers::ContainsSubstring("alpha"));
        REQUIRE_THAT(out.str(), Catch::Matchers::ContainsSubstring("beta"));
      }
    }

    WHEN("We use GetOrAddCardValue to create a new suit") {
      deck.GetOrAddCardValue<double>("newsuit", "card1", 7.0);

      THEN("The new suit appears in ordering") {
        auto suits = deck.GetSuitsInOrder();
        REQUIRE(std::find(suits.begin(), suits.end(), "newsuit") != suits.end());
      }
      THEN("WriteDeck includes the new suit") {
        std::ostringstream out;
        deck.WriteDeck(out);
        REQUIRE_THAT(out.str(), Catch::Matchers::ContainsSubstring("newsuit"));
      }
    }
  }
}

TEST_CASE("Deck - GetVector correct order for large vectors") {
  GIVEN("A deck with a vector of more than 9 elements") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "<suit1>\n";
    // Build a 12-element vector inline
    ss << "vec = 0,1,2,3,4,5,6,7,8,9,10,11\n";
    deck.Build(ss);

    WHEN("We retrieve the vector") {
      auto vec = deck.GetVector<double>("suit1", "vec");

      THEN("All 12 elements are present in numeric order") {
        REQUIRE(vec.size() == 12);
        for (int i = 0; i < 12; i++) {
          FLOAT_REQUIRE(vec[i], static_cast<double>(i));
        }
      }
    }
  }
}

TEST_CASE("Deck - Second Build call preserves nested suit references") {
  GIVEN("A deck with a nested suit (gas/eos)") {
    Rummy::Deck deck;
    std::stringstream ss1;
    ss1 << "<gas>\n"
        << "gamma = 1.4\n"
        << "<../eos>\n"       
        << "cv = 1.0\n";
    deck.Build(ss1);

    WHEN("We build again with an expression referencing gas/eos.cv") {
      std::stringstream ss2;
      ss2 << "<result>\n"
          << "val = gas.eos.cv * 2\n";
      deck.Build(ss2);

      THEN("No phantom suit is created") {
        // Should have: '/', 'gas', 'gas/eos', 'result' — not a spurious 'gas_eos'
        REQUIRE_FALSE(deck.DoesSuitExist("gas_eos"));
        REQUIRE(deck.DoesSuitExist("gas/eos"));
      }
      THEN("The computed value is correct") {
        FLOAT_REQUIRE(deck.GetCardValue<double>("result", "val"), 2.0);
      }
    }
  }
}

TEST_CASE("Deck - WriteDeck preserves declaration order") {
  GIVEN("A deck where 'b' is declared before 'a' but 'a' references 'b'") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "<suit1>\n"
       << "z_base = 10\n"        // alphabetically last, declared first
       << "a_derived = z_base * 2\n"; // alphabetically first, declared second
    deck.Build(ss);

    WHEN("We write and re-read the deck") {
      std::ostringstream out;
      deck.WriteDeck(out);

      Rummy::Deck deck2;
      std::istringstream in(out.str());
      deck2.Build(in);

      THEN("The re-read deck has the correct value") {
        FLOAT_REQUIRE(deck2.GetCardValue<double>("suit1", "a_derived"), 20.0);
      }
    }
  }
}

TEST_CASE("Deck - Multiline continuation preserves comment") {
  GIVEN("A deck with a multiline value") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "<suit1>\n"
       << "x = 1 +  &  # part one\n"
       << "    2        # part two\n";
    deck.Build(ss);

    THEN("The value is the sum of both lines") {
      FLOAT_REQUIRE(deck.GetCardValue<double>("suit1", "x"), 3.0);
    }
    THEN("Comments from continuation lines are joined") {
      REQUIRE_THAT(deck.GetCard("suit1", "x").GetComment(),
                   Catch::Matchers::ContainsSubstring("part one"));
      REQUIRE_THAT(deck.GetCard("suit1", "x").GetComment(),
                   Catch::Matchers::ContainsSubstring("part two"));
    }
  }
}

TEST_CASE("Deck - Relative suit name (..)") {
  GIVEN("A deck using relative suit syntax") {
    Rummy::Deck deck;
    std::stringstream ss;
    ss << "<gas>\n"
       << "name = \"hydrogen\"\n"
       << "<../eos>\n"
       << "gamma = 1.67\n"
       << "type = \"ideal\"\n"
       << "cv = 1.0 / (gamma - 1.0)\n";
    deck.Build(ss);

    THEN("Suits are correctly named") {
      REQUIRE(deck.DoesSuitExist("gas"));
      REQUIRE(deck.DoesSuitExist("gas/eos"));
    }
    THEN("Card values are correct") {
      REQUIRE(deck.GetCardValue<std::string>("gas", "name") == "hydrogen");
      REQUIRE(deck.GetCardValue<std::string>("gas/eos", "type") == "ideal");
      FLOAT_REQUIRE_TOL(deck.GetCardValue<double>("gas/eos", "gamma"), 1.67, 1e-12);
      FLOAT_REQUIRE_TOL(deck.GetCardValue<double>("gas/eos", "cv"),
                        1.0 / (1.67 - 1.0), 1e-12);
    }
  }
}
