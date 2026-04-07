#pragma once

#include <string>
#include <string_view>

namespace axc::detail {

bool isIdentifierStart(unsigned char ch);
bool isIdentifierContinue(unsigned char ch);
std::string unescapeString(std::string_view source);

}  // namespace axc::detail
