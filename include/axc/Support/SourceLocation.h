#pragma once

#include <cstddef>

namespace axc {

struct SourceLocation {
    std::size_t offset = 0;
};

struct SourceRange {
    SourceLocation begin {};
    SourceLocation end {};
};

}  // namespace axc
