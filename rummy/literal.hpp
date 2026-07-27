//========================================================================================
// (C) (or copyright) 2026. Triad National Security, LLC. All rights reserved.
//========================================================================================
#ifndef RUMMY_LITERAL_HPP_
#define RUMMY_LITERAL_HPP_

#include <string>

#include <pips/value_types.hpp>

namespace Rummy {

// Return a pips source literal that recreates value without losing precision.
// INSTANCE values cannot be represented as scalar literals and throw.
std::string ToPipsLiteral(const pips::Value &value);

} // namespace Rummy

#endif // RUMMY_LITERAL_HPP_
