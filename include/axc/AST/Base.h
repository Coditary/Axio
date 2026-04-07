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

/// @brief Parsed annotation attached to a declaration.
struct Annotation {
    std::string name {};
    std::vector<std::string> arguments {};
    SourceRange range {};
};

/// @brief Parsed preprocessor directive preserved in the translation unit.
struct PreprocessorDirective {
    std::string name {};
    std::vector<std::string> arguments {};
    SourceRange range {};
};

/// @brief Ownership/storage modifiers that can decorate a type spelling.
enum class TypeModifier {
    Ref,
    Weak,
    Unique,
};

/// @brief Surface-language type spelling captured in the AST.
///
/// This structure stores the unresolved type name and syntactic modifiers as
/// parsed from the source. Later phases decide how those spellings map to
/// semantic types and LLVM storage types.
struct Type {
    std::string name {};
    std::vector<TypeModifier> modifiers {};
    std::size_t pointerDepth = 0;
    std::vector<std::optional<std::size_t>> arrayExtents {};
    SourceRange range {};

    /// @brief Returns whether this is the unqualified `void` type.
    [[nodiscard]] bool isVoid() const { return name == "void" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `int` type.
    [[nodiscard]] bool isInt() const { return name == "int" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `str` type.
    [[nodiscard]] bool isString() const { return name == "str" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    /// @brief Returns whether this is the unqualified `bool` type.
    [[nodiscard]] bool isBool() const { return name == "bool" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
};

/// @brief Distinguishes initializer/storage forms such as ARC, weak, or unique.
enum class InitKind {
    Value,
    Arc,
    Weak,
    Unique,
    ArrayLiteral,
};

/// @brief Compile-time parameter declaration used in brace-call syntax.
struct CompileArg {
    std::string name {};
    Type type {};
    SourceRange range {};
};

}  // namespace axc
