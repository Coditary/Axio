#include "../Internal/SemaInternal.h"

#include "axc/Support/QualifiedName.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

SemaImpl::SemaImpl(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool SemaImpl::analyze(TranslationUnit& translationUnit) {
    globalSymbols_.clear();
    globalSymbolTypes_.clear();
    buildEnumTables(translationUnit);
    buildClassTables(translationUnit);
    recordFunctionSignatures(translationUnit);
    for (const auto& decl : translationUnit.declarations) {
        validateDecl(*decl);
    }
    return !diagnostics_.hasErrors();
}

ValueInfo::Ownership SemaImpl::ownershipFromType(const Type& type) const {
    for (TypeModifier modifier : type.modifiers) {
        if (modifier == TypeModifier::Ref) {
            return ValueInfo::Ownership::Ref;
        }
        if (modifier == TypeModifier::Weak) {
            return ValueInfo::Ownership::Weak;
        }
        if (modifier == TypeModifier::Unique) {
            return ValueInfo::Ownership::Unique;
        }
    }
    if (classInfos_.contains(type.name)) {
        return ValueInfo::Ownership::Arc;
    }
    return ValueInfo::Ownership::Value;
}

ValueInfo::Ownership SemaImpl::ownershipFromInitKind(InitKind kind) const {
    switch (kind) {
        case InitKind::Value:
            return ValueInfo::Ownership::Value;
        case InitKind::Arc:
            return ValueInfo::Ownership::Arc;
        case InitKind::Weak:
            return ValueInfo::Ownership::Weak;
        case InitKind::Unique:
            return ValueInfo::Ownership::Unique;
        case InitKind::ArrayLiteral:
            return ValueInfo::Ownership::Value;
    }
    return ValueInfo::Ownership::Unknown;
}

bool SemaImpl::isPointerLike(const Type& type) const {
    return type.pointerDepth > 0 || type.name == "str" || classInfos_.contains(type.name);
}

bool SemaImpl::typeSupportsNullability(const Type& type) const {
    return isPointerLike(type) || ownershipFromType(type) != ValueInfo::Ownership::Value;
}

std::optional<Type> SemaImpl::exprType(const Expr& expr, std::size_t valueIndex) const {
    switch (expr.kind) {
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto it = symbolTypes_.find(ref.name);
            if (it != symbolTypes_.end()) {
                return it->second;
            }
            break;
        }
        case ExprKind::NullLiteral: {
            Type type;
            type.name = "null";
            type.range = expr.range;
            return type;
        }
        case ExprKind::StringLiteral: {
            Type type;
            type.name = "str";
            type.range = expr.range;
            return type;
        }
        case ExprKind::BoolLiteral: {
            Type type;
            type.name = "bool";
            type.range = expr.range;
            return type;
        }
        case ExprKind::CharLiteral: {
            Type type;
            type.name = "char";
            type.range = expr.range;
            return type;
        }
        case ExprKind::FloatLiteral: {
            Type type;
            type.name = "f64";
            type.range = expr.range;
            return type;
        }
        case ExprKind::IntegerLiteral: {
            Type type;
            type.name = "int";
            type.range = expr.range;
            return type;
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            Type type;
            if (init.initKind == InitKind::ArrayLiteral) {
                if (!init.values.empty()) {
                    auto elementType = exprType(*init.values.front());
                    if (elementType.has_value()) {
                        type = *elementType;
                    }
                }
                if (type.name.empty()) {
                    type.name = "int";
                }
                type.arrayExtents.push_back(init.values.size());
            } else {
                type.name = init.typeName;
            }
            type.range = expr.range;
            if (classInfos_.contains(init.typeName) || (init.initKind != InitKind::Value && init.initKind != InitKind::ArrayLiteral)) {
                ++type.pointerDepth;
            }
            return type;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len" && call.runtimeArguments.size() == 1) {
                    Type type;
                    type.name = "int";
                    type.range = expr.range;
                    return type;
                }
            }
            if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto it = functionReturnTypes_.find(*calleeName);
                if (it != functionReturnTypes_.end() && valueIndex < it->second.size()) {
                    return it->second[valueIndex];
                }
            }
            break;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            if (unary.op == UnaryOp::IsNonNull) {
                Type type;
                type.name = "bool";
                type.range = expr.range;
                return type;
            }
            if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement || unary.op == UnaryOp::PostIncrement ||
                unary.op == UnaryOp::PostDecrement) {
                auto operandType = exprType(*unary.operand);
                if (operandType.has_value()) {
                    operandType->range = expr.range;
                    return operandType;
                }
                break;
            }
            auto operandType = exprType(*unary.operand);
            if (!operandType.has_value()) {
                break;
            }
            if (unary.op == UnaryOp::AddressOf) {
                ++operandType->pointerDepth;
                operandType->range = expr.range;
                return operandType;
            }
            if (unary.op == UnaryOp::Dereference) {
                if (operandType->pointerDepth > 0) {
                    --operandType->pointerDepth;
                } else if (!operandType->modifiers.empty()) {
                    operandType->modifiers.erase(operandType->modifiers.begin());
                }
                operandType->range = expr.range;
                return operandType;
            }
            break;
        }
        case ExprKind::Cast: {
            Type type = static_cast<const CastExpr&>(expr).targetType;
            type.range = expr.range;
            return type;
        }
        default:
            break;
    }
    return std::nullopt;
}

std::optional<std::string> SemaImpl::moduleQualifiedName(const Expr& expr) const {
    auto name = qualifiedNameFromExpr(expr);
    if (!name.has_value()) {
        return std::nullopt;
    }
    const std::string root = name->substr(0, name->find('.'));
    if (symbols_.contains(root)) {
        return std::nullopt;
    }
    return name;
}

bool SemaImpl::isKnownDeclRefName(const std::string& name) const {
    if (name.empty()) {
        return false;
    }
    if (name == "len") {
        return true;
    }
    if (symbols_.contains(name) || globalSymbols_.contains(name) || functionReturnCount_.contains(name) || enumInfos_.contains(name) ||
        classInfos_.contains(name) || structFields_.contains(name)) {
        return true;
    }
    for (const auto& [_, info] : enumInfos_) {
        if (info.values.contains(name)) {
            return true;
        }
        for (const auto& [variant, params] : info.paramValues) {
            if (params.contains(name)) {
                return true;
            }
            if (info.values.contains(variant + "." + name)) {
                return true;
            }
        }
    }
    return false;
}

void SemaImpl::recordFunctionSignatures(TranslationUnit& translationUnit) {
    for (const auto& decl : translationUnit.declarations) {
        if (decl->kind == DeclKind::GlobalVar) {
            const auto& global = static_cast<const GlobalVarDecl&>(*decl);
            Type globalType = global.type;
            if (globalType.name.empty() && global.initializer) {
                auto inferredType = exprType(*global.initializer);
                if (inferredType.has_value()) {
                    globalType = *inferredType;
                }
            }
            if (!globalType.name.empty()) {
                globalSymbolTypes_[global.name] = globalType;
                globalSymbols_[global.name] = ValueInfo {ownershipFromType(globalType), globalType.name,
                                                         typeSupportsNullability(globalType), global.mutableStorage};
            }
            continue;
        }
        if (decl->kind != DeclKind::Function) {
            continue;
        }
        const auto& fn = static_cast<const FunctionDecl&>(*decl);
        std::vector<ValueInfo::Ownership> ownerships;
        ownerships.reserve(fn.runtimeParameters.size());
        for (const auto& param : fn.runtimeParameters) {
            ownerships.push_back(ownershipFromType(param.type));
        }
        functionParamOwnership_[fn.name] = std::move(ownerships);
        functionArgumentCount_[fn.name] = fn.compileParameters.size() + fn.runtimeParameters.size();
        functionReturnCount_[fn.name] = fn.returnValueCount();
        functionReturnTypes_[fn.name] = fn.returnTypes;
    }
}

void SemaImpl::buildClassTables(TranslationUnit& translationUnit) {
    for (const auto& decl : translationUnit.declarations) {
        if (decl->kind == DeclKind::Struct) {
            const auto& structDecl = static_cast<const StructDecl&>(*decl);
            auto& fields = structFields_[structDecl.name];
            auto& fieldTypes = structFieldTypes_[structDecl.name];
            for (const auto& field : structDecl.fields) {
                fields.insert(field.name);
                fieldTypes[field.name] = field.type;
            }
            continue;
        }
        if (decl->kind != DeclKind::Class) {
            continue;
        }
        const auto& classDecl = static_cast<const ClassDecl&>(*decl);
        ClassInfo info;
        for (const auto& includedStruct : classDecl.includedStructs) {
            auto structIt = structFields_.find(includedStruct);
            if (structIt != structFields_.end()) {
                info.fields.insert(structIt->second.begin(), structIt->second.end());
            }
            auto structTypeIt = structFieldTypes_.find(includedStruct);
            if (structTypeIt != structFieldTypes_.end()) {
                info.fieldTypes.insert(structTypeIt->second.begin(), structTypeIt->second.end());
            }
        }
        for (const auto& member : classDecl.members) {
            info.fields.insert(member.name);
            if (!member.type.name.empty()) {
                info.fieldTypes[member.name] = member.type;
            }
        }
        for (const auto& method : classDecl.methods) {
            info.methods.insert(method->name);
            std::vector<ValueInfo::Ownership> ownerships;
            const auto& fn = static_cast<const FunctionDecl&>(*method);
            info.methodArgumentCounts[method->name] = fn.compileParameters.size() + fn.runtimeParameters.size() - (fn.runtimeParameters.empty() ? 0U : 1U);
            for (const auto& param : fn.runtimeParameters) {
                ownerships.push_back(ownershipFromType(param.type));
            }
            info.methodParamOwnerships[method->name] = std::move(ownerships);
        }
        classInfos_[classDecl.name] = std::move(info);
    }
}

ValueInfo SemaImpl::inferExpr(const Expr& expr) const {
    switch (expr.kind) {
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            const bool isClass = classInfos_.contains(init.typeName);
            return ValueInfo {
                isClass && init.initKind == InitKind::Value ? ValueInfo::Ownership::Arc : ownershipFromInitKind(init.initKind),
                init.typeName,
                false
            };
        }
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto it = symbols_.find(ref.name);
            if (it != symbols_.end()) {
                return it->second;
            }
            return ValueInfo {};
        }
        case ExprKind::NullLiteral:
            return ValueInfo {ValueInfo::Ownership::Unknown, "", true};
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len" && call.runtimeArguments.size() == 1) {
                    return ValueInfo {ValueInfo::Ownership::Value, "int", false};
                }
            }
            if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto typeIt = functionReturnTypes_.find(*calleeName);
                if (typeIt != functionReturnTypes_.end() && !typeIt->second.empty()) {
                    const Type& returnType = typeIt->second.front();
                    return ValueInfo {ownershipFromType(returnType), returnType.name, false};
                }
                auto sigIt = functionReturnCount_.find(*calleeName);
                if (sigIt != functionReturnCount_.end()) {
                    return ValueInfo {ValueInfo::Ownership::Unknown, "", false};
                }
            }
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (auto baseType = exprType(*member.base); baseType.has_value() && classInfos_.contains(baseType->name)) {
                    const std::string loweredName = baseType->name + "." + member.member;
                    auto typeIt = functionReturnTypes_.find(loweredName);
                    if (typeIt != functionReturnTypes_.end() && !typeIt->second.empty()) {
                        const Type& returnType = typeIt->second.front();
                        return ValueInfo {ownershipFromType(returnType), returnType.name, false};
                    }
                }
            }
            return ValueInfo {};
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            if (unary.op == UnaryOp::AddressOf || unary.op == UnaryOp::Dereference) {
                ValueInfo value = inferExpr(*unary.operand);
                value.nullable = false;
                return value;
            }
            if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement || unary.op == UnaryOp::PostIncrement ||
                unary.op == UnaryOp::PostDecrement) {
                return inferExpr(*unary.operand);
            }
            return ValueInfo {};
        }
        case ExprKind::Cast: {
            const auto& cast = static_cast<const CastExpr&>(expr);
            return ValueInfo {ownershipFromType(cast.targetType), cast.targetType.name, typeSupportsNullability(cast.targetType)};
        }
        default:
            return ValueInfo {};
    }
}

std::size_t SemaImpl::exprValueCount(const Expr& expr) const {
    if (expr.kind == ExprKind::Call) {
        const auto& call = static_cast<const CallExpr&>(expr);
        auto calleeName = moduleQualifiedName(*call.callee);
        if (!calleeName.has_value()) {
            return 1;
        }
        auto it = functionReturnCount_.find(*calleeName);
        if (it != functionReturnCount_.end()) {
            return it->second;
        }
    }
    return 1;
}

std::size_t SemaImpl::flattenedValueCount(const std::vector<std::unique_ptr<Expr>>& values) const {
    std::size_t count = 0;
    for (const auto& value : values) {
        count += exprValueCount(*value);
    }
    return count;
}

bool SemaImpl::containsDeclRef(const Expr& expr, const std::string& name) const {
    switch (expr.kind) {
        case ExprKind::DeclRef:
            return static_cast<const DeclRefExpr&>(expr).name == name;
        case ExprKind::Unary:
            return containsDeclRef(*static_cast<const UnaryExpr&>(expr).operand, name);
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            return containsDeclRef(*binary.lhs, name) || containsDeclRef(*binary.rhs, name);
        }
        case ExprKind::Cast:
            return containsDeclRef(*static_cast<const CastExpr&>(expr).value, name);
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (containsDeclRef(*call.callee, name)) {
                return true;
            }
            for (const auto& arg : call.compileArguments) {
                if (containsDeclRef(*arg, name)) {
                    return true;
                }
            }
            for (const auto& arg : call.runtimeArguments) {
                if (containsDeclRef(*arg, name)) {
                    return true;
                }
            }
            return false;
        }
        case ExprKind::Member:
            return containsDeclRef(*static_cast<const MemberExpr&>(expr).base, name);
        case ExprKind::Initializer: {
            for (const auto& value : static_cast<const InitializerExpr&>(expr).values) {
                if (containsDeclRef(*value, name)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

void SemaImpl::collectRepeatedUniqueUses(const Expr& expr, std::unordered_set<std::string>& seen, const std::string& message) {
    switch (expr.kind) {
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto it = symbols_.find(ref.name);
            if (it != symbols_.end() && it->second.ownership == ValueInfo::Ownership::Unique) {
                if (!seen.insert(ref.name).second) {
                    diagnostics_.error(expr.range, message);
                }
            }
            break;
        }
        case ExprKind::Unary:
            collectRepeatedUniqueUses(*static_cast<const UnaryExpr&>(expr).operand, seen, message);
            break;
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            collectRepeatedUniqueUses(*binary.lhs, seen, message);
            collectRepeatedUniqueUses(*binary.rhs, seen, message);
            break;
        }
        case ExprKind::Cast:
            collectRepeatedUniqueUses(*static_cast<const CastExpr&>(expr).value, seen, message);
            break;
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            collectRepeatedUniqueUses(*call.callee, seen, message);
            for (const auto& arg : call.compileArguments) {
                collectRepeatedUniqueUses(*arg, seen, message);
            }
            for (const auto& arg : call.runtimeArguments) {
                collectRepeatedUniqueUses(*arg, seen, message);
            }
            break;
        }
        case ExprKind::Member:
            collectRepeatedUniqueUses(*static_cast<const MemberExpr&>(expr).base, seen, message);
            break;
        case ExprKind::Initializer: {
            for (const auto& value : static_cast<const InitializerExpr&>(expr).values) {
                collectRepeatedUniqueUses(*value, seen, message);
            }
            break;
        }
        default:
            break;
    }
}

void SemaImpl::requireSingleValue(const Expr& expr, const std::string& message) {
    if (exprValueCount(expr) > 1) {
        diagnostics_.error(expr.range, message);
    }
}

void SemaImpl::buildEnumTables(TranslationUnit& translationUnit) {
    for (auto& decl : translationUnit.declarations) {
        if (decl->kind != DeclKind::Enum) {
            continue;
        }

        auto& enumDecl = static_cast<EnumDecl&>(*decl);
        EnumInfo info;
        info.isFlags = enumDecl.isFlags;
        std::uint64_t nextValue = 0;
        std::uint64_t nextFlagBit = 0;

        for (auto& element : enumDecl.elements) {
            std::uint64_t assigned = 0;
            if (enumDecl.isFlags) {
                assigned = 1ULL << nextFlagBit++;
            } else {
                assigned = nextValue++;
            }
            element.constantValue = assigned;
            info.values[element.name] = assigned;
            if (!enumDecl.isFlags) {
                info.maxOrdinal = assigned > info.maxOrdinal ? assigned : info.maxOrdinal;
            }

            if (!enumDecl.parameters.empty() && element.payloadValues.size() == enumDecl.parameters.size()) {
                for (std::size_t i = 0; i < enumDecl.parameters.size(); ++i) {
                    const auto value = evalExpr(*element.payloadValues[i]);
                    if (value.has_value()) {
                        info.paramValues[element.name][enumDecl.parameters[i].name] = *value;
                    }
                }
            }

            if (!element.nestedDecls.empty()) {
                std::uint64_t nestedValue = 0;
                std::uint64_t nestedFlagBit = 0;
                for (const auto& nestedDecl : element.nestedDecls) {
                    if (nestedDecl->kind != DeclKind::Enum) {
                        continue;
                    }
                    const auto& nestedEnum = static_cast<const EnumDecl&>(*nestedDecl);
                    for (const auto& nestedElement : nestedEnum.elements) {
                        const std::uint64_t nestedAssigned = element.isFlagGroup ? (1ULL << (nextFlagBit + nestedFlagBit++)) : nestedValue++;
                        info.values[element.name + "." + nestedElement.name] = nestedAssigned;
                    }
                }
                if (element.isFlagGroup) {
                    nextFlagBit += nestedFlagBit;
                }
            }
        }

        enumInfos_[enumDecl.name] = std::move(info);
    }
}

}  // namespace axc
