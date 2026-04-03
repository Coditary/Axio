#pragma once

#include <optional>
#include <string>

#include "axc/AST/Expr.h"

namespace axc {

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

inline std::string lastQualifiedSegment(const std::string& name) {
    const std::size_t pos = name.rfind('.');
    return pos == std::string::npos ? name : name.substr(pos + 1);
}

}  // namespace axc
