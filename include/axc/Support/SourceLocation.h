#pragma once

#include <cstddef>

namespace axc {

/// @brief Byte offset into the loaded UTF-8 source buffer.
struct SourceLocation {
    std::size_t offset = 0;
};

/// @brief Half-open source range using byte offsets.
struct SourceRange {
    SourceLocation begin {};
    SourceLocation end {};
};

}  // namespace axc
