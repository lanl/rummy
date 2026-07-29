//========================================================================================
// (C) (or copyright) 2026. Triad National Security, LLC. All rights reserved.
//========================================================================================

#include "literal.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <pips/value.hpp>

namespace Rummy {

std::string ToPipsLiteral(const pips::Value &value) {
  std::ostringstream os;
  switch (value.type) {
  case pips::ValueType::STRING: {
    const std::string text = value.as.string ? value.as.string->str : std::string{};
    if (text.find_first_of("\"\n\r") != std::string::npos) {
      throw std::runtime_error(
          "Rummy restart state cannot represent strings containing quotes or newlines");
    }
    os << '"' << text << '"';
    break;
  }
  case pips::ValueType::BOOL:
    os << (value.as.boolean ? "true" : "false");
    break;
  case pips::ValueType::NUMBER:
    os << std::setprecision(std::numeric_limits<double>::max_digits10)
       << value.as.number;
    break;
  case pips::ValueType::NIL:
    os << "nil";
    break;
  case pips::ValueType::VECTOR: {
    os << '[';
    auto *vector = AS_VECTOR(value);
    if (vector != nullptr) {
      for (std::size_t i = 0; i < vector->elements.size(); ++i) {
        if (i != 0) os << ", ";
        os << ToPipsLiteral(vector->elements[i]);
      }
    }
    os << ']';
    break;
  }
  case pips::ValueType::INSTANCE:
    throw std::runtime_error("Cannot emit an instance as a scalar pips literal");
  }
  return os.str();
}

} // namespace Rummy
