#include "SemaInternal.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

void SemaImpl::validateDecl(const Decl& decl) {
    switch (decl.kind) {
        case DeclKind::Import:
            break;
        case DeclKind::Function:
            validateFunction(static_cast<const FunctionDecl&>(decl));
            break;
        case DeclKind::Class:
            validateClass(static_cast<const ClassDecl&>(decl));
            break;
        case DeclKind::Struct:
        case DeclKind::Enum:
            break;
    }
}

void SemaImpl::validateClass(const ClassDecl& decl) {
    std::unordered_set<std::string> memberNames;
    std::unordered_set<std::string> methodNames;
    for (const auto& includedStruct : decl.includedStructs) {
        if (!structFields_.contains(includedStruct)) {
            diagnostics_.error(decl.range, "class includes unknown struct '" + includedStruct + "'");
        }
    }
    for (const auto& member : decl.members) {
        if (!memberNames.insert(member.name).second) {
            diagnostics_.error(member.range, "duplicate class member '" + member.name + "'");
        }
        if (!member.dynamicValue && member.type.name.empty()) {
            diagnostics_.error(member.range, "class members need a declared type unless they are dynamic fields");
        }
        if (member.dynamicValue) {
            if (member.type.name.empty()) {
                diagnostics_.error(member.range, "dynamic class fields must declare an explicit type");
            }
            validateExpr(*member.dynamicValue);
            requireSingleValue(*member.dynamicValue, "dynamic class fields must evaluate to a single value");
            if (containsDeclRef(*member.dynamicValue, member.name)) {
                diagnostics_.error(member.range, "dynamic class fields cannot directly reference themselves");
            }
        }
    }
    for (const auto& method : decl.methods) {
        if (!methodNames.insert(method->name).second) {
            diagnostics_.error(method->range, "duplicate class method '" + method->name + "'");
        }
        if (memberNames.contains(method->name)) {
            diagnostics_.error(method->range, "class member and method share the name '" + method->name + "'");
        }
        validateDecl(*method);
    }
}

void SemaImpl::validateFunction(const FunctionDecl& fn) {
    symbols_.clear();
    symbolTypes_.clear();
    consumedUnique_.clear();

    if (fn.returnTypes.empty()) {
        diagnostics_.warning(fn.range, "function without explicit return type defaults to void semantics in this prototype");
    }
    if (!fn.compileParameters.empty()) {
        diagnostics_.warning(fn.range, "compile-time function parameters are parsed but not yet specialized in codegen");
    }

    for (const auto& param : fn.runtimeParameters) {
        ValueInfo info {ownershipFromType(param.type), param.type.name, isPointerLike(param.type)};
        symbols_[param.name] = info;
        symbolTypes_[param.name] = param.type;
        if (info.ownership == ValueInfo::Ownership::Arc) {
            diagnostics_.warning(param.range, "ARC ownership is parsed, but full retain/release semantics are not implemented yet");
        }
        if (info.ownership == ValueInfo::Ownership::Weak) {
            diagnostics_.warning(param.range, "weak ownership is parsed, but full weak lifetime semantics are not implemented yet");
        }
    }

    if (fn.body) {
        validateStmt(*fn.body, fn);
    }
}

void SemaImpl::validateStmt(const Stmt& stmt, const FunctionDecl& fn) {
    switch (stmt.kind) {
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            for (const auto& child : block.statements) {
                validateStmt(*child, fn);
            }
            break;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            const std::size_t returnedValueCount = flattenedValueCount(ret.values);
            if (fn.returnTypes.size() <= 1 && returnedValueCount > 1) {
                diagnostics_.error(ret.range, "return value count does not match function signature");
            }
            if (fn.returnTypes.size() > 1 && returnedValueCount != fn.returnTypes.size()) {
                diagnostics_.error(ret.range, "multi-return statement must return the same number of values as declared");
            }
            std::unordered_set<std::string> returnedUniqueValues;
            for (const auto& value : ret.values) {
                validateExpr(*value);
                collectRepeatedUniqueUses(*value, returnedUniqueValues, "unique values cannot be returned more than once from the same statement");
                if (value->kind == ExprKind::DeclRef) {
                    const auto& ref = static_cast<const DeclRefExpr&>(*value);
                    auto it = symbols_.find(ref.name);
                    if (it != symbols_.end() && it->second.ownership == ValueInfo::Ownership::Ref) {
                        diagnostics_.error(value->range, "ref values cannot escape by being returned");
                    }
                }
            }
            break;
        }
        case StmtKind::Expr:
            validateExpr(*static_cast<const ExprStmt&>(stmt).expression);
            break;
        case StmtKind::Let: {
            const auto& letStmt = static_cast<const LetStmt&>(stmt);
            if (letStmt.bindings.empty()) {
                diagnostics_.error(letStmt.range, "let declaration must bind at least one name");
                break;
            }
            if (letStmt.bindings.size() == 1 && letStmt.bindings.front().explicitType.name.empty() && !letStmt.initializer) {
                diagnostics_.error(letStmt.range, "let declaration needs either a type or an initializer");
            }
            if (letStmt.initializer) {
                validateExpr(*letStmt.initializer);
                const std::size_t initializerValueCount = exprValueCount(*letStmt.initializer);
                if (letStmt.bindings.size() == 1 && initializerValueCount > 1) {
                    diagnostics_.error(letStmt.initializer->range, "multi-return calls cannot initialize a single binding");
                }
                if (letStmt.bindings.size() > 1 && initializerValueCount != letStmt.bindings.size()) {
                    diagnostics_.error(letStmt.initializer->range, "destructuring let requires the same number of values as bindings");
                }
                ValueInfo value = inferExpr(*letStmt.initializer);
                const LetBinding& firstBinding = letStmt.bindings.front();
                if (letStmt.initializer->kind == ExprKind::NullLiteral && !firstBinding.explicitType.name.empty() && !typeSupportsNullability(firstBinding.explicitType)) {
                    diagnostics_.error(letStmt.initializer->range, "null cannot initialize a value that is not nullable");
                }
                if (!firstBinding.explicitType.name.empty() && ownershipFromType(firstBinding.explicitType) == ValueInfo::Ownership::Ref &&
                    value.ownership != ValueInfo::Ownership::Value && value.ownership != ValueInfo::Ownership::Unknown) {
                    diagnostics_.error(letStmt.range, "ref values cannot be stored from owning heap initializers");
                }
                if (!firstBinding.explicitType.name.empty() && ownershipFromType(firstBinding.explicitType) != ValueInfo::Ownership::Ref &&
                    value.ownership == ValueInfo::Ownership::Ref) {
                    diagnostics_.error(letStmt.range, "ref values cannot be stored in owning or value bindings");
                }
                if (!firstBinding.explicitType.name.empty() && ownershipFromType(firstBinding.explicitType) == ValueInfo::Ownership::Weak &&
                    value.ownership != ValueInfo::Ownership::Arc && value.ownership != ValueInfo::Ownership::Weak && value.ownership != ValueInfo::Ownership::Unknown) {
                    diagnostics_.error(letStmt.range, "weak values must originate from ARC or weak references");
                }
                if (!firstBinding.explicitType.name.empty() && ownershipFromType(firstBinding.explicitType) == ValueInfo::Ownership::Arc &&
                    value.ownership == ValueInfo::Ownership::Unique) {
                    diagnostics_.warning(letStmt.range, "assigning unique values into ARC storage is parsed but move-to-ARC semantics are not implemented yet");
                }
                for (const auto& binding : letStmt.bindings) {
                    Type bindingType = binding.explicitType;
                    if (bindingType.name.empty()) {
                        auto inferredType = exprType(*letStmt.initializer, letStmt.bindings.size() == 1 ? 0 : (&binding - &letStmt.bindings.front()));
                        if (inferredType.has_value()) {
                            bindingType = *inferredType;
                        }
                    }
                    if (!bindingType.name.empty()) {
                        symbolTypes_[binding.name] = bindingType;
                    }
                    const bool nullable = letStmt.initializer->kind == ExprKind::NullLiteral ||
                        (bindingType.name.empty() ? value.nullable : typeSupportsNullability(bindingType) && value.nullable);
                    symbols_[binding.name] = !binding.explicitType.name.empty()
                        ? ValueInfo {ownershipFromType(binding.explicitType), binding.explicitType.name, nullable}
                        : ValueInfo {value.ownership, bindingType.name, nullable};
                }
                if (value.ownership == ValueInfo::Ownership::Unique && letStmt.initializer->kind == ExprKind::DeclRef) {
                    consumedUnique_.insert(static_cast<const DeclRefExpr&>(*letStmt.initializer).name);
                }
            } else {
                for (const auto& binding : letStmt.bindings) {
                    if (binding.explicitType.name.empty()) {
                        diagnostics_.error(binding.range, "destructuring let bindings need either an initializer or an explicit type");
                        continue;
                    }
                    symbolTypes_[binding.name] = binding.explicitType;
                    symbols_[binding.name] = ValueInfo {ownershipFromType(binding.explicitType), binding.explicitType.name, isPointerLike(binding.explicitType)};
                }
            }
            break;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            validateExpr(*ifStmt.condition);
            requireSingleValue(*ifStmt.condition, "if conditions must be single values");
            const auto savedSymbols = symbols_;
            const auto savedTypes = symbolTypes_;
            const auto* unary = dynamic_cast<const UnaryExpr*>(ifStmt.condition.get());
            if (unary != nullptr && unary->op == UnaryOp::IsNonNull && unary->operand->kind == ExprKind::DeclRef) {
                const auto& ref = static_cast<const DeclRefExpr&>(*unary->operand);
                auto it = symbols_.find(ref.name);
                if (it != symbols_.end()) {
                    it->second.nullable = false;
                }
                validateStmt(*ifStmt.thenBlock, fn);
                symbols_ = savedSymbols;
                symbolTypes_ = savedTypes;
                if (ifStmt.elseBranch) {
                    validateStmt(*ifStmt.elseBranch, fn);
                    symbols_ = savedSymbols;
                    symbolTypes_ = savedTypes;
                }
                break;
            }
            if (unary != nullptr && unary->op == UnaryOp::LogicalNot) {
                const auto* inner = dynamic_cast<const UnaryExpr*>(unary->operand.get());
                if (inner != nullptr && inner->op == UnaryOp::IsNonNull && inner->operand->kind == ExprKind::DeclRef) {
                    if (ifStmt.elseBranch) {
                        const auto& ref = static_cast<const DeclRefExpr&>(*inner->operand);
                        validateStmt(*ifStmt.thenBlock, fn);
                        symbols_ = savedSymbols;
                        symbolTypes_ = savedTypes;
                        auto it = symbols_.find(ref.name);
                        if (it != symbols_.end()) {
                            it->second.nullable = false;
                        }
                        validateStmt(*ifStmt.elseBranch, fn);
                        symbols_ = savedSymbols;
                        symbolTypes_ = savedTypes;
                        break;
                    }
                }
            }
            validateStmt(*ifStmt.thenBlock, fn);
            symbols_ = savedSymbols;
            symbolTypes_ = savedTypes;
            if (ifStmt.elseBranch) {
                validateStmt(*ifStmt.elseBranch, fn);
                symbols_ = savedSymbols;
                symbolTypes_ = savedTypes;
            }
            break;
        }
    }
}

}  // namespace axc
