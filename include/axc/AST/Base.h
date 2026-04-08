#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

/// @brief Visibility of top-level declarations in the module system.
enum class Visibility {
    Private,
    Public,
};

/// @brief Surface-language type spelling captured in the AST.
///
/// This structure stores the unresolved type name and syntactic modifiers as
/// parsed from the source. Later phases decide how those spellings map to
/// semantic types and LLVM storage types.
struct Type {
    std::string name {};
    std::size_t pointerDepth = 0;
    std::vector<std::optional<std::size_t>> arrayExtents {};
    SourceRange range {};

    /// @brief Returns whether this is the unqualified `void` type.
    [[nodiscard]] bool isVoid() const { return name == "void" && pointerDepth == 0 && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `int` type.
    [[nodiscard]] bool isInt() const { return name == "int" && pointerDepth == 0 && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `str` type.
    [[nodiscard]] bool isString() const { return name == "str" && pointerDepth == 0 && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `bool` type.
    [[nodiscard]] bool isBool() const { return name == "bool" && pointerDepth == 0 && arrayExtents.empty(); }
};

/// @brief Distinguishes initializer/storage forms supported by the MVP.
enum class InitKind {
    Value,
    ArrayLiteral,
};

}  // namespace axc
