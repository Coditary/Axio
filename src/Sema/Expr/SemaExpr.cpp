/// @file
/// @brief Expression semantic analysis and constant evaluation support.

#include "../Internal/SemaInternal.h"

#include <cstdint>

#include "axc/Support/Diagnostic.h"
#include "axc/Support/QualifiedName.h"

namespace axc {

namespace {

const DeclRefExpr* rootAssignedDeclRef(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::DeclRef:
            return static_cast<const DeclRefExpr*>(&expr);
        case ExprKind::Member:
            return rootAssignedDeclRef(*static_cast<const MemberExpr&>(expr).base);
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            if (unary.op == UnaryOp::Dereference) {
                return rootAssignedDeclRef(*unary.operand);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

}  // namespace

std::optional<std::uint64_t> SemaImpl::evalExpr(const Expr& expr) const {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral:
            return static_cast<std::uint64_t>(static_cast<const IntegerLiteralExpr&>(expr).value);
        case ExprKind::BoolLiteral:
            return static_cast<const BoolLiteralExpr&>(expr).value ? 1ULL : 0ULL;
        case ExprKind::CharLiteral:
            return static_cast<unsigned char>(static_cast<const CharLiteralExpr&>(expr).value);
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            for (const auto& [_, info] : enumInfos_) {
                auto it = info.values.find(ref.name);
                if (it != info.values.end()) {
                    return it->second;
                }
            }
            return std::nullopt;
        }
        case ExprKind::Member: {
            if (auto qualifiedName = qualifiedNameFromExpr(expr); qualifiedName.has_value()) {
                for (const auto& [enumName, info] : enumInfos_) {
                    const std::string prefix = enumName + ".";
                    if (qualifiedName->rfind(prefix, 0) == 0) {
                        const std::string suffix = qualifiedName->substr(prefix.size());
                        auto it = info.values.find(suffix);
                        if (it != info.values.end()) {
                            return it->second;
                        }
                    }
                }
            }
            return std::nullopt;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.callee->kind != ExprKind::DeclRef || call.arguments.size() != 1) {
                return std::nullopt;
            }
            const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
            auto enumIt = enumInfos_.find(callee.name);
            if (enumIt == enumInfos_.end()) {
                return std::nullopt;
            }
            auto ordinal = evalExpr(*call.arguments.front());
            if (!ordinal.has_value() || *ordinal > enumIt->second.maxOrdinal) {
                return std::nullopt;
            }
            return ordinal;
        }
        case ExprKind::Initializer:
            return std::nullopt;
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            const auto lhs = evalExpr(*binary.lhs);
            const auto rhs = evalExpr(*binary.rhs);
            if (!lhs.has_value() || !rhs.has_value()) {
                return std::nullopt;
            }
            switch (binary.op) {
                case BinaryOp::Add:
                    return *lhs + *rhs;
                case BinaryOp::Sub:
                    return *lhs - *rhs;
                case BinaryOp::Mul:
                    return *lhs * *rhs;
                case BinaryOp::Div:
                    return *rhs == 0 ? std::nullopt : std::optional<std::uint64_t>(*lhs / *rhs);
                case BinaryOp::Mod:
                    return *rhs == 0 ? std::nullopt : std::optional<std::uint64_t>(*lhs % *rhs);
                case BinaryOp::BitAnd:
                    return *lhs & *rhs;
                case BinaryOp::BitOr:
                    return *lhs | *rhs;
                case BinaryOp::BitXor:
                    return *lhs ^ *rhs;
                case BinaryOp::ShiftLeft:
                    return *lhs << *rhs;
                case BinaryOp::ShiftRight:
                    return *lhs >> *rhs;
                case BinaryOp::Equal:
                    return *lhs == *rhs;
                case BinaryOp::NotEqual:
                    return *lhs != *rhs;
                case BinaryOp::Less:
                    return *lhs < *rhs;
                case BinaryOp::LessEqual:
                    return *lhs <= *rhs;
                case BinaryOp::Greater:
                    return *lhs > *rhs;
                case BinaryOp::GreaterEqual:
                    return *lhs >= *rhs;
                case BinaryOp::LogicalAnd:
                    return static_cast<std::uint64_t>((*lhs != 0) && (*rhs != 0));
                case BinaryOp::LogicalOr:
                    return static_cast<std::uint64_t>((*lhs != 0) || (*rhs != 0));
                case BinaryOp::Assign:
                    return std::nullopt;
            }
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            const auto operand = evalExpr(*unary.operand);
            if (!operand.has_value()) {
                return std::nullopt;
            }
            switch (unary.op) {
                case UnaryOp::LogicalNot:
                    return static_cast<std::uint64_t>(*operand == 0);
                case UnaryOp::BitwiseNot:
                    return ~*operand;
                case UnaryOp::Negate:
                    return static_cast<std::uint64_t>(-static_cast<std::int64_t>(*operand));
                case UnaryOp::PreIncrement:
                    return *operand + 1;
                case UnaryOp::PreDecrement:
                    return *operand - 1;
                case UnaryOp::PostIncrement:
                case UnaryOp::PostDecrement:
                case UnaryOp::AddressOf:
                case UnaryOp::Dereference:
                    return std::nullopt;
            }
        }
        case ExprKind::FloatLiteral:
        case ExprKind::StringLiteral:
            return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> SemaImpl::enumTypeName(const Expr& expr) const {
    if (expr.kind != ExprKind::Member) {
        return std::nullopt;
    }
    const auto& member = static_cast<const MemberExpr&>(expr);
    auto baseName = moduleQualifiedName(*member.base);
    if (baseName.has_value() && enumInfos_.contains(*baseName)) {
        return baseName;
    }
    return std::nullopt;
}

void SemaImpl::validateExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            validateExpr(*binary.lhs);
            validateExpr(*binary.rhs);
            requireSingleValue(*binary.lhs, "binary operators require single-value operands");
            requireSingleValue(*binary.rhs, "binary operators require single-value operands");
            if (binary.op == BinaryOp::Assign) {
                const DeclRefExpr* lhsRef = rootAssignedDeclRef(*binary.lhs);
                const ValueInfo* lhsInfo = nullptr;
                if (lhsRef != nullptr) {
                    auto lhsIt = symbols_.find(lhsRef->name);
                    if (lhsIt != symbols_.end()) {
                        lhsInfo = &lhsIt->second;
                    } else {
                        auto globalIt = globalSymbols_.find(lhsRef->name);
                        if (globalIt != globalSymbols_.end()) {
                            lhsInfo = &globalIt->second;
                        }
                    }
                }
                if (lhsRef == nullptr) {
                    diagnostics_.error(binary.range, "assignment requires an assignable left-hand side");
                } else if (lhsInfo != nullptr && !lhsInfo->mutableStorage) {
                    diagnostics_.error(binary.range, "cannot assign to const storage '" + lhsRef->name + "'");
                }
            }
            break;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            validateExpr(*unary.operand);
            requireSingleValue(*unary.operand, "unary operators require a single-value operand");
            if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement || unary.op == UnaryOp::PostIncrement ||
                unary.op == UnaryOp::PostDecrement) {
                const DeclRefExpr* target = rootAssignedDeclRef(*unary.operand);
                const ValueInfo* targetInfo = nullptr;
                if (target != nullptr) {
                    auto it = symbols_.find(target->name);
                    if (it != symbols_.end()) {
                        targetInfo = &it->second;
                    } else {
                        auto globalIt = globalSymbols_.find(target->name);
                        if (globalIt != globalSymbols_.end()) {
                            targetInfo = &globalIt->second;
                        }
                    }
                }
                if (target == nullptr) {
                    diagnostics_.error(unary.range, "increment and decrement require an assignable operand");
                } else if (targetInfo != nullptr && !targetInfo->mutableStorage) {
                    diagnostics_.error(unary.range, "cannot mutate const storage '" + target->name + "'");
                }
            }
            break;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            validateExpr(*call.callee);
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len") {
                    if (call.arguments.size() != 1) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to 'len'");
                        break;
                    }
                    validateExpr(*call.arguments[0]);
                    auto argumentType = exprType(*call.arguments[0]);
                    if (!argumentType.has_value() || argumentType->arrayExtents.empty()) {
                        diagnostics_.error(call.range, "len expects an array argument");
                    }
                    break;
                }
            }
            for (const auto& arg : call.arguments) {
                validateExpr(*arg);
                requireSingleValue(*arg, "call arguments must be single values");
            }
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                auto baseType = exprType(*member.base);
                if (baseType.has_value() && classInfos_.contains(baseType->name)) {
                    const auto& info = classInfos_.at(baseType->name);
                    auto countIt = info.methodArgumentCounts.find(member.member);
                    if (countIt != info.methodArgumentCounts.end() && call.arguments.size() != countIt->second) {
                        diagnostics_.error(call.range, "wrong number of arguments for method '" + member.member + "'");
                    }
                    if (!info.methods.contains(member.member)) {
                        diagnostics_.error(call.range, "class '" + baseType->name + "' has no method '" + member.member + "'");
                    }
                }
            } else if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto countIt = functionArgumentCount_.find(*calleeName);
                if (countIt != functionArgumentCount_.end() && call.arguments.size() != countIt->second) {
                    diagnostics_.error(call.range, "wrong number of arguments for call to '" + *calleeName + "'");
                }
            }
            break;
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            const bool maybeModulePath = member.base->kind == ExprKind::DeclRef && !isKnownDeclRefName(static_cast<const DeclRefExpr&>(*member.base).name);
            if (!maybeModulePath) {
                validateExpr(*member.base);
                requireSingleValue(*member.base, "member access requires a single-value base expression");
            }

            auto baseType = exprType(*member.base);
            if (baseType.has_value() && classInfos_.contains(baseType->name)) {
                const auto& info = classInfos_.at(baseType->name);
                if (!info.fields.contains(member.member) && !info.methods.contains(member.member)) {
                    diagnostics_.error(member.range, "class '" + baseType->name + "' has no member '" + member.member + "'");
                }
                break;
            }
            if (baseType.has_value() && structFields_.contains(baseType->name)) {
                const auto& fields = structFields_.at(baseType->name);
                if (!fields.contains(member.member)) {
                    diagnostics_.error(member.range, "struct '" + baseType->name + "' has no field '" + member.member + "'");
                }
                break;
            }
            if (auto qualifiedName = qualifiedNameFromExpr(expr); qualifiedName.has_value()) {
                for (const auto& [enumName, info] : enumInfos_) {
                    const std::string prefix = enumName + ".";
                    if (qualifiedName->rfind(prefix, 0) == 0 && info.values.contains(qualifiedName->substr(prefix.size()))) {
                        return;
                    }
                }
                if (functionArgumentCount_.contains(*qualifiedName) || functionReturnTypes_.contains(*qualifiedName) || enumInfos_.contains(*qualifiedName)) {
                    return;
                }
            }
            break;
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            for (const auto& value : init.values) {
                validateExpr(*value);
                requireSingleValue(*value, "initializer values must be single values");
            }
            break;
        }
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            if (!isKnownDeclRefName(ref.name)) {
                diagnostics_.error(ref.range, "unknown symbol '" + ref.name + "'");
            }
            break;
        }
        case ExprKind::IntegerLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::CharLiteral:
        case ExprKind::StringLiteral:
            break;
    }
}

}  // namespace axc
