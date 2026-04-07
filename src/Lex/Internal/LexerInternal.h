#pragma once

#include <string>
#include <string_view>

namespace axc::detail {

/// @brief Returns whether `ch` may start an identifier.
bool isIdentifierStart(unsigned char ch);
/// @brief Returns whether `ch` may continue an identifier.
bool isIdentifierContinue(unsigned char ch);
/// @brief Decode escape sequences inside a string literal payload.
std::string unescapeString(std::string_view source);

}  // namespace axc::detail
