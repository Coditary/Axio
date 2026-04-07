/// @file
/// @brief Expression semantic analysis, constant evaluation, and type inference support.

#include "../Internal/SemaInternal.h"

#include <cstdint>

#include "axc/Support/QualifiedName.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

namespace {

std::optional<std::uint64_t> lookupEnumValueBySuffix(const std::unordered_map<std::string, EnumInfo>& enumInfos,
                                                     std::string_view qualifiedName) {
    for (const auto& [enumName, info] : enumInfos) {
        auto exactIt = info.values.find(std::string(qualifiedName));
        if (exactIt != info.values.end()) {
            return exactIt->second;
        }
        const std::string suffix = "." + std::string(qualifiedName);
        for (const auto& [name, value] : info.values) {
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return value;
            }
        }
    }
    return std::nullopt;
}

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
            for (const auto& [enumName, info] : enumInfos_) {
                auto valueIt = info.values.find(static_cast<const DeclRefExpr&>(expr).name);
                if (valueIt != info.values.end()) {
                    return valueIt->second;
                }
            }
            return std::nullopt;
        }
        case ExprKind::Member: {
            if (auto qualifiedName = moduleQualifiedName(expr); qualifiedName.has_value()) {
                for (const auto& [enumName, info] : enumInfos_) {
                    const std::string prefix = enumName + ".";
                    if (qualifiedName->rfind(prefix, 0) == 0) {
                        const std::string suffix = qualifiedName->substr(prefix.size());
                        auto valueIt = info.values.find(suffix);
                        if (valueIt != info.values.end()) {
                            return valueIt->second;
                        }
                    }
                }
            }
            const auto& member = static_cast<const MemberExpr&>(expr);
            if (member.base->kind == ExprKind::Member) {
                if (auto nestedName = qualifiedNameFromExpr(expr); nestedName.has_value()) {
                    if (auto value = lookupEnumValueBySuffix(enumInfos_, *nestedName); value.has_value()) {
                        return value;
                    }
                }
            }
            if (member.base->kind == ExprKind::DeclRef) {
                const auto& base = static_cast<const DeclRefExpr&>(*member.base);
                auto enumIt = enumInfos_.find(base.name);
                if (enumIt != enumInfos_.end()) {
                    auto valueIt = enumIt->second.values.find(member.member);
                    if (valueIt != enumIt->second.values.end()) {
                        return valueIt->second;
                    }
                    for (const auto& [variant, params] : enumIt->second.paramValues) {
                        auto paramIt = params.find(member.member);
                        if (paramIt != params.end()) {
                            return paramIt->second;
                        }
                    }
                }
            }
            if (member.base->kind == ExprKind::Call) {
                const auto& call = static_cast<const CallExpr&>(*member.base);
                if (call.callee->kind == ExprKind::DeclRef && !call.runtimeArguments.empty()) {
                    const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                    auto enumIt = enumInfos_.find(callee.name);
                    if (enumIt != enumInfos_.end() && call.runtimeArguments[0]->kind == ExprKind::IntegerLiteral) {
                        const auto ordinal = static_cast<std::uint64_t>(static_cast<const IntegerLiteralExpr&>(*call.runtimeArguments[0]).value);
                        for (const auto& [variant, value] : enumIt->second.values) {
                            if (value == ordinal) {
                                auto paramsIt = enumIt->second.paramValues.find(variant);
                                if (paramsIt != enumIt->second.paramValues.end()) {
                                    auto paramIt = paramsIt->second.find(member.member);
                                    if (paramIt != paramsIt->second.end()) {
                                        return paramIt->second;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return std::nullopt;
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            auto enumIt = enumInfos_.find(init.typeName);
            if (enumIt == enumInfos_.end() || !enumIt->second.isFlags) {
                return std::nullopt;
            }
            std::uint64_t value = 0;
            for (const auto& item : init.values) {
                const auto itemValue = evalExpr(*item);
                if (!itemValue.has_value()) {
                    return std::nullopt;
                }
                value |= *itemValue;
            }
            return value;
        }
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
                case BinaryOp::Set:
                    return *lhs | *rhs;
                case BinaryOp::Unset:
                    return *lhs & ~*rhs;
                case BinaryOp::Toggle:
                    return *lhs ^ *rhs;
                case BinaryOp::Is:
                    return static_cast<std::uint64_t>((*lhs & *rhs) == *rhs);
                case BinaryOp::IsNot:
                    return static_cast<std::uint64_t>((*lhs & *rhs) != *rhs);
                case BinaryOp::InRange: {
                    if (binary.rhs->kind != ExprKind::Range) {
                        return std::nullopt;
                    }
                    const auto& range = static_cast<const RangeExpr&>(*binary.rhs);
                    const auto start = evalExpr(*range.start);
                    const auto end = evalExpr(*range.end);
                    if (!start.has_value() || !end.has_value()) {
                        return std::nullopt;
                    }

                    const auto enumName = enumTypeName(*range.start);
                    if (enumName.has_value()) {
                        const auto infoIt = enumInfos_.find(*enumName);
                        if (infoIt != enumInfos_.end()) {
                            const std::uint64_t maxOrdinal = infoIt->second.maxOrdinal;
                            std::uint64_t current = *start;
                            do {
                                if (current == *lhs) {
                                    return 1ULL;
                                }
                                if (current == *end) {
                                    return range.inclusive ? 1ULL : 0ULL;
                                }
                                current = current == maxOrdinal ? 0 : current + 1;
                            } while (current != *start);
                            return 0ULL;
                        }
                    }

                    if (*start <= *end) {
                        return static_cast<std::uint64_t>(*lhs >= *start && (range.inclusive ? *lhs <= *end : *lhs < *end));
                    }
                    return static_cast<std::uint64_t>(*lhs >= *start || (range.inclusive ? *lhs <= *end : *lhs < *end));
                }
                case BinaryOp::Assign:
                    return std::nullopt;
            }
        }
        case ExprKind::Cast:
            return evalExpr(*static_cast<const CastExpr&>(expr).value);
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
                case UnaryOp::PostIncrement:
                    return *operand;
                case UnaryOp::PreDecrement:
                    return *operand - 1;
                case UnaryOp::PostDecrement:
                    return *operand;
                case UnaryOp::AddressOf:
                case UnaryOp::Dereference:
                case UnaryOp::IsNonNull:
                    return std::nullopt;
            }
        }
        case ExprKind::Range:
        case ExprKind::FloatLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::NullLiteral:
        case ExprKind::Call:
        case ExprKind::CompileCall:
        case ExprKind::Dialect:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> SemaImpl::enumTypeName(const Expr& expr) const {
    if (expr.kind == ExprKind::Member) {
        const auto& member = static_cast<const MemberExpr&>(expr);
        if (auto baseName = moduleQualifiedName(*member.base); baseName.has_value() && enumInfos_.contains(*baseName)) {
            return *baseName;
        }
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
                if (lhsRef != nullptr && lhsInfo != nullptr && !lhsInfo->mutableStorage) {
                    diagnostics_.error(binary.range, "cannot assign to const storage '" + lhsRef->name + "'");
                }
            }
            if (binary.op == BinaryOp::Assign && binary.rhs->kind == ExprKind::DeclRef) {
                const auto& rhsRef = static_cast<const DeclRefExpr&>(*binary.rhs);
                auto rhsIt = symbols_.find(rhsRef.name);
                if (rhsIt != symbols_.end() && rhsIt->second.ownership == ValueInfo::Ownership::Ref) {
                    if (binary.lhs->kind == ExprKind::DeclRef) {
                        const auto& lhsRef = static_cast<const DeclRefExpr&>(*binary.lhs);
                        auto lhsIt = symbols_.find(lhsRef.name);
                        if (lhsIt != symbols_.end() && lhsIt->second.ownership != ValueInfo::Ownership::Ref) {
                            diagnostics_.error(binary.range, "ref values cannot be assigned into owning or value storage");
                        }
                    } else {
                        diagnostics_.error(binary.range, "ref values cannot escape through assignment");
                    }
                }
            }
            if (binary.op == BinaryOp::InRange) {
                diagnostics_.warning(binary.range, "range membership is parsed but not yet lowered in codegen");
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
            if (unary.op == UnaryOp::IsNonNull && unary.operand->kind == ExprKind::DeclRef) {
                const auto& ref = static_cast<const DeclRefExpr&>(*unary.operand);
                auto it = symbols_.find(ref.name);
                if (it != symbols_.end() && !it->second.nullable) {
                    diagnostics_.warning(unary.range, "null-check postfix '?' used on a value that is not known to be nullable");
                }
            }
            break;
        }
        case ExprKind::Cast: {
            const auto& cast = static_cast<const CastExpr&>(expr);
            validateExpr(*cast.value);
            requireSingleValue(*cast.value, "cast expressions require a single-value operand");
            break;
        }
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::CharLiteral:
            break;
        case ExprKind::Range: {
            const auto& range = static_cast<const RangeExpr&>(expr);
            validateExpr(*range.start);
            validateExpr(*range.end);
            break;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            validateExpr(*call.callee);
            const std::size_t suppliedArgumentCount = call.compileArguments.size() + call.runtimeArguments.size();
            std::vector<const Expr*> suppliedArguments;
            suppliedArguments.reserve(suppliedArgumentCount);
            for (const auto& argument : call.compileArguments) {
                suppliedArguments.push_back(argument.get());
            }
            for (const auto& argument : call.runtimeArguments) {
                suppliedArguments.push_back(argument.get());
            }
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len") {
                    if (call.runtimeArguments.size() != 1 || !call.compileArguments.empty()) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to 'len'");
                        break;
                    }
                    validateExpr(*call.runtimeArguments[0]);
                    auto argumentType = exprType(*call.runtimeArguments[0]);
                    if (!argumentType.has_value() || argumentType->arrayExtents.empty()) {
                        diagnostics_.error(call.range, "len expects an array argument");
                    }
                    break;
                }
            }
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (member.nullSafe && member.base->kind == ExprKind::DeclRef) {
                    const auto& ref = static_cast<const DeclRefExpr&>(*member.base);
                    auto it = symbols_.find(ref.name);
                    if (it != symbols_.end() && !it->second.nullable) {
                        diagnostics_.warning(call.range, "null-safe call used on a value that is not known to be nullable");
                    }
                }
                std::optional<Type> baseType = exprType(*member.base);
                if (baseType.has_value() && classInfos_.contains(baseType->name)) {
                    const auto& info = classInfos_.at(baseType->name);
                    auto countIt = info.methodArgumentCounts.find(member.member);
                    if (countIt != info.methodArgumentCounts.end() && suppliedArgumentCount != countIt->second) {
                        diagnostics_.error(call.range, "wrong number of arguments for method '" + member.member + "'");
                    }
                    auto methodIt = info.methodParamOwnerships.find(member.member);
                    if (methodIt != info.methodParamOwnerships.end()) {
                        const std::size_t compileParamCount = countIt != info.methodArgumentCounts.end() && countIt->second >= methodIt->second.size() - 1
                            ? countIt->second - (methodIt->second.size() - 1)
                            : 0;
                        for (std::size_t i = compileParamCount; i < suppliedArguments.size() && i - compileParamCount + 1 < methodIt->second.size(); ++i) {
                            if (suppliedArguments[i]->kind != ExprKind::DeclRef) {
                                continue;
                            }
                            const auto& argRef = static_cast<const DeclRefExpr&>(*suppliedArguments[i]);
                            auto symIt = symbols_.find(argRef.name);
                            if (symIt == symbols_.end()) {
                                continue;
                            }
                            if (methodIt->second[i - compileParamCount + 1] == ValueInfo::Ownership::Unique) {
                                if (symIt->second.ownership != ValueInfo::Ownership::Unique) {
                                    diagnostics_.error(suppliedArguments[i]->range, "unique parameters require unique arguments");
                                } else {
                                    consumedUnique_.insert(argRef.name);
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& arg : call.compileArguments) {
                validateExpr(*arg);
                requireSingleValue(*arg, "call arguments must be single values");
            }
            for (const auto& arg : call.runtimeArguments) {
                validateExpr(*arg);
                requireSingleValue(*arg, "call arguments must be single values");
                if (arg->kind == ExprKind::DeclRef) {
                    const auto& ref = static_cast<const DeclRefExpr&>(*arg);
                    if (consumedUnique_.contains(ref.name)) {
                        diagnostics_.error(arg->range, "unique values cannot be used after they have been moved");
                    }
                }
            }
            std::unordered_set<std::string> uniqueCallValues;
            for (const auto& arg : call.runtimeArguments) {
                collectRepeatedUniqueUses(*arg, uniqueCallValues, "unique values cannot be passed more than once to the same call");
            }
            if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto countIt = functionArgumentCount_.find(*calleeName);
                if (countIt != functionArgumentCount_.end() && suppliedArgumentCount != countIt->second) {
                    diagnostics_.error(call.range, "wrong number of arguments for call to '" + *calleeName + "'");
                }
                auto fnIt = functionParamOwnership_.find(*calleeName);
                if (fnIt != functionParamOwnership_.end()) {
                    const std::size_t compileParamCount = countIt != functionArgumentCount_.end() && countIt->second >= fnIt->second.size()
                        ? countIt->second - fnIt->second.size()
                        : 0;
                    for (std::size_t i = compileParamCount; i < suppliedArguments.size() && i - compileParamCount < fnIt->second.size(); ++i) {
                        if (suppliedArguments[i]->kind != ExprKind::DeclRef) {
                            continue;
                        }
                        const auto& argRef = static_cast<const DeclRefExpr&>(*suppliedArguments[i]);
                        auto symIt = symbols_.find(argRef.name);
                        if (symIt == symbols_.end()) {
                            continue;
                        }
                        if (fnIt->second[i - compileParamCount] == ValueInfo::Ownership::Unique) {
                            if (symIt->second.ownership != ValueInfo::Ownership::Unique) {
                                diagnostics_.error(suppliedArguments[i]->range, "unique parameters require unique arguments");
                                } else {
                                    consumedUnique_.insert(argRef.name);
                                }
                        }
                    }
                }
            }
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (member.base->kind == ExprKind::DeclRef) {
                    const auto& ref = static_cast<const DeclRefExpr&>(*member.base);
                    auto it = symbols_.find(ref.name);
                    if (it != symbols_.end() && it->second.nullable && !member.nullSafe) {
                        diagnostics_.warning(call.range, "method call may dereference a nullable value; use '?.' or guard it with 'if value?'");
                    }
                }
                std::optional<Type> baseType = exprType(*member.base);
                if (baseType.has_value() && classInfos_.contains(baseType->name)) {
                    const auto& info = classInfos_.at(baseType->name);
                    if (!info.methods.contains(member.member)) {
                        diagnostics_.error(call.range, "class '" + baseType->name + "' has no method '" + member.member + "'");
                    }
                }
            }
            break;
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            validateExpr(*member.base);
            requireSingleValue(*member.base, "member access requires a single-value base expression");
            if (member.base->kind == ExprKind::DeclRef) {
                const auto& ref = static_cast<const DeclRefExpr&>(*member.base);
                auto it = symbols_.find(ref.name);
                if (it != symbols_.end() && it->second.nullable && !member.nullSafe) {
                    diagnostics_.warning(member.range, "member access may dereference a nullable value; use '?.' or guard it with 'if value?'");
                }
                if (member.nullSafe) {
                    if (it != symbols_.end() && !it->second.nullable) {
                        diagnostics_.warning(member.range, "null-safe member access used on a value that is not known to be nullable");
                    }
                }
            }
            std::optional<Type> baseType = exprType(*member.base);
            if (baseType.has_value() && classInfos_.contains(baseType->name)) {
                const auto& info = classInfos_.at(baseType->name);
                if (!info.fields.contains(member.member) && !info.methods.contains(member.member)) {
                    diagnostics_.error(member.range, "class '" + baseType->name + "' has no member '" + member.member + "'");
                }
            }
            break;
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            for (const auto& value : init.values) {
                validateExpr(*value);
            }
            if (enumInfos_.contains(init.typeName) && enumInfos_[init.typeName].isFlags) {
                const auto& enumInfo = enumInfos_.at(init.typeName);
                for (const auto& value : init.values) {
                    auto evaluated = evalExpr(*value);
                    if (!evaluated.has_value() && value->kind == ExprKind::Member) {
                        if (auto nestedName = qualifiedNameFromExpr(*value); nestedName.has_value()) {
                            if (auto nestedValue = lookupEnumValueBySuffix(enumInfos_, *nestedName); nestedValue.has_value()) {
                                evaluated = nestedValue;
                            }
                        }
                    }
                    if (!evaluated.has_value()) {
                        diagnostics_.error(expr.range, "flag enum initializer must contain only compile-time enum members");
                        break;
                    }
                }
            }
            break;
        }
        case ExprKind::CompileCall: {
            const auto& call = static_cast<const CompileCallExpr&>(expr);
            for (const auto& arg : call.arguments) {
                validateExpr(*arg);
            }
            break;
        }
        case ExprKind::Dialect:
            diagnostics_.warning(expr.range, "dialect blocks are parsed but not yet lowered");
            break;
        case ExprKind::IntegerLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::NullLiteral:
            break;
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            if (!isKnownDeclRefName(ref.name)) {
                diagnostics_.error(ref.range, "unknown symbol '" + ref.name + "'");
            }
            break;
        }
    }
}

}  // namespace axc
