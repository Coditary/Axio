#pragma once

#include <optional>
#include <string>

#include "axc/AST/Expr.h"

namespace axc {

/// @brief Resolve an expression to a dotted qualified-name string when possible.
inline std::optional<std::string> qualifiedNameFromExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::DeclRef:
            return static_cast<const DeclRefExpr&>(expr).name;
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            if (member.nullSafe) {
                return std::nullopt;
            }
            auto base = qualifiedNameFromExpr(*member.base);
            if (!base.has_value()) {
                return std::nullopt;
            }
            return *base + "." + member.member;
        }
        default:
            return std::nullopt;
    }
}

/// @brief Return the last segment of a dotted qualified name.
inline std::string lastQualifiedSegment(const std::string& name) {
    const std::size_t pos = name.rfind('.');
    return pos == std::string::npos ? name : name.substr(pos + 1);
}

}  // namespace axc
