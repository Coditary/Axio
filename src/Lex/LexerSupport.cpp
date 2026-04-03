#include "LexerInternal.h"

#include <cctype>

namespace axc::detail {

bool isIdentifierStart(unsigned char ch) {
    return std::isalpha(ch) || ch == '_' || ch >= 0x80;
}

bool isIdentifierContinue(unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch >= 0x80;
}

std::string unescapeString(std::string_view source) {
    std::string result;
    result.reserve(source.size());

    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '\\') {
            result.push_back(source[i]);
            continue;
        }

        if (i + 1 >= source.size()) {
            result.push_back('\\');
            break;
        }

        ++i;
        switch (source[i]) {
            case 'n':
                result.push_back('\n');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            default:
                result.push_back(source[i]);
                break;
        }
    }

    return result;
}

}  // namespace axc::detail
