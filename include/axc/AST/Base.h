#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

enum class Visibility {
    Private,
    Public,
};

struct Annotation {
    std::string name {};
    std::vector<std::string> arguments {};
    SourceRange range {};
};

struct PreprocessorDirective {
    std::string name {};
    std::vector<std::string> arguments {};
    SourceRange range {};
};

enum class TypeModifier {
    Ref,
    Weak,
    Unique,
};

struct Type {
    std::string name {};
    std::vector<TypeModifier> modifiers {};
    std::size_t pointerDepth = 0;
    std::vector<std::optional<std::size_t>> arrayExtents {};
    SourceRange range {};

    [[nodiscard]] bool isVoid() const { return name == "void" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    [[nodiscard]] bool isInt() const { return name == "int" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    [[nodiscard]] bool isString() const { return name == "str" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
    [[nodiscard]] bool isBool() const { return name == "bool" && pointerDepth == 0 && modifiers.empty() && arrayExtents.empty(); }
};

enum class InitKind {
    Value,
    Arc,
    Weak,
    Unique,
    ArrayLiteral,
};

struct CompileArg {
    std::string name {};
    Type type {};
    SourceRange range {};
};

}  // namespace axc
