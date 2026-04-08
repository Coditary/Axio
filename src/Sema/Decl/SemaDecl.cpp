/// @file
/// @brief Declaration and statement semantic validation, including scope and switch rules.

#include "../Internal/SemaInternal.h"

#include <algorithm>

#include "axc/Support/InlineLlvm.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::vector<std::string> SemaImpl::missingEnumSwitchCases(const SwitchStmt& stmt, const Type& conditionType) const {
    std::vector<std::string> missing;
    auto enumIt = enumInfos_.find(conditionType.name);
    if (enumIt == enumInfos_.end() || enumIt->second.isFlags) {
        return missing;
    }

    std::unordered_set<std::uint64_t> coveredValues;
    bool allPatternsConstant = true;
    for (const auto& switchCase : stmt.cases) {
        if (switchCase.isDefault) {
            return {};
        }
        for (const auto& pattern : switchCase.patterns) {
            const auto expanded = expandSwitchPatternValues(pattern, conditionType);
            if (expanded.empty()) {
                allPatternsConstant = false;
                continue;
            }
            for (const auto& [value, _] : expanded) {
                coveredValues.insert(value);
            }
        }
    }

    if (!allPatternsConstant) {
        return {"<non-constant case pattern>"};
    }

    std::vector<std::pair<std::uint64_t, std::string>> orderedValues;
    orderedValues.reserve(enumIt->second.values.size());
    for (const auto& [name, value] : enumIt->second.values) {
        if (name.find('.') == std::string::npos) {
            orderedValues.emplace_back(value, name);
        }
    }
    std::sort(orderedValues.begin(), orderedValues.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    for (const auto& [value, name] : orderedValues) {
        if (!coveredValues.contains(value)) {
            missing.push_back(conditionType.name + "." + name);
        }
    }
    return missing;
}

std::vector<std::pair<std::uint64_t, std::string>> SemaImpl::expandSwitchPatternValues(const SwitchCasePattern& pattern,
                                                                                        const Type& conditionType) const {
    std::vector<std::pair<std::uint64_t, std::string>> values;
    if (pattern.value == nullptr) {
        return values;
    }

    auto formatValue = [&](std::uint64_t value) -> std::string {
        auto enumIt = enumInfos_.find(conditionType.name);
        if (enumIt != enumInfos_.end() && !enumIt->second.isFlags) {
            for (const auto& [name, ordinal] : enumIt->second.values) {
                if (name.find('.') == std::string::npos && ordinal == value) {
                    return conditionType.name + "." + name;
                }
            }
        }
        return std::to_string(value);
    };

    if (auto evaluated = evalExpr(*pattern.value); evaluated.has_value()) {
        values.emplace_back(*evaluated, formatValue(*evaluated));
    }
    return values;
}

void SemaImpl::validateDecl(const Decl& decl) {
    switch (decl.kind) {
        case DeclKind::Import:
            break;
        case DeclKind::GlobalVar:
            validateGlobal(static_cast<const GlobalVarDecl&>(decl));
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

void SemaImpl::validateGlobal(const GlobalVarDecl& decl) {
    if (!decl.type.name.empty()) {
        globalSymbolTypes_[decl.name] = decl.type;
        globalSymbols_[decl.name] = ValueInfo {ownershipFromType(decl.type), decl.type.name,
                                               typeSupportsNullability(decl.type), decl.mutableStorage};
    }
    if (decl.type.name.empty() && !decl.initializer) {
        diagnostics_.error(decl.range, "global declaration needs either a type or an initializer");
        return;
    }
    if (decl.initializer) {
        validateExpr(*decl.initializer);
        requireSingleValue(*decl.initializer, "global initializers must evaluate to a single value");
    }
}

void SemaImpl::validateClass(const ClassDecl& decl) {
    std::unordered_set<std::string> memberNames;
    std::unordered_set<std::string> methodNames;
    const ClassInfo* classInfo = nullptr;
    auto classIt = classInfos_.find(decl.name);
    if (classIt != classInfos_.end()) {
        classInfo = &classIt->second;
    }
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
            const auto savedSymbols = symbols_;
            const auto savedTypes = symbolTypes_;
            if (classInfo != nullptr) {
                for (const auto& [name, type] : classInfo->fieldTypes) {
                    symbols_[name] = ValueInfo {ownershipFromType(type), type.name, typeSupportsNullability(type), false};
                    symbolTypes_[name] = type;
                }
            }
            validateExpr(*member.dynamicValue);
            requireSingleValue(*member.dynamicValue, "dynamic class fields must evaluate to a single value");
            symbols_ = savedSymbols;
            symbolTypes_ = savedTypes;
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
    symbols_ = globalSymbols_;
    symbolTypes_ = globalSymbolTypes_;
    consumedUnique_.clear();

    if (!fn.receiverType.empty()) {
        auto classIt = classInfos_.find(fn.receiverType);
        if (classIt != classInfos_.end()) {
            for (const auto& [name, type] : classIt->second.fieldTypes) {
                symbols_[name] = ValueInfo {ownershipFromType(type), type.name, typeSupportsNullability(type), false};
                symbolTypes_[name] = type;
            }
        }
    }

    for (const auto& param : fn.runtimeParameters) {
        ValueInfo info {ownershipFromType(param.type), param.type.name, false, !param.isConst};
        symbols_[param.name] = info;
        symbolTypes_[param.name] = param.type;
    }

    for (const auto& param : fn.compileParameters) {
        ValueInfo info {ownershipFromType(param.type), param.type.name, false, !param.isConst};
        symbols_[param.name] = info;
        symbolTypes_[param.name] = param.type;
    }

    if (fn.isLlvm) {
        if (fn.body) {
            diagnostics_.error(fn.range, "llvm functions cannot use Axio statements in their body");
        }
        if (fn.llvmBody.empty()) {
            diagnostics_.error(fn.range, "llvm functions require a non-empty llvm body");
            return;
        }

        std::string moduleText;
        std::string errorMessage;
        if (!buildInlineLlvmModuleText(fn, fn.name, moduleText, errorMessage)) {
            diagnostics_.error(fn.llvmBodyRange.begin.offset == 0 && fn.llvmBodyRange.end.offset == 0 ? fn.range : fn.llvmBodyRange, errorMessage);
            return;
        }
        if (!verifyInlineLlvmModuleText(moduleText, fn.name, errorMessage)) {
            diagnostics_.error(fn.llvmBodyRange.begin.offset == 0 && fn.llvmBodyRange.end.offset == 0 ? fn.range : fn.llvmBodyRange,
                               "invalid llvm function body: " + errorMessage);
        }
        return;
    }

    if (fn.body) {
        validateStmt(*fn.body, fn, 0, 0);
    }
}

void SemaImpl::validateStmt(const Stmt& stmt, const FunctionDecl& fn) {
    validateStmt(stmt, fn, 0, 0);
}

void SemaImpl::validateStmt(const Stmt& stmt, const FunctionDecl& fn, std::size_t loopDepth, std::size_t switchDepth) {
    switch (stmt.kind) {
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            const auto savedSymbols = symbols_;
            const auto savedTypes = symbolTypes_;
            for (const auto& child : block.statements) {
                validateStmt(*child, fn, loopDepth, switchDepth);
            }
            symbols_ = savedSymbols;
            symbolTypes_ = savedTypes;
            break;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            const std::size_t returnedValueCount = flattenedValueCount(ret.values);
            if (fn.returnsVoid()) {
                if (returnedValueCount > 0) {
                    diagnostics_.error(ret.range, "void functions cannot return a value");
                }
                break;
            }
            if (fn.returnTypes.size() <= 1 && returnedValueCount > 1) {
                diagnostics_.error(ret.range, "return value count does not match function signature");
            }
            if (fn.returnTypes.size() > 1 && returnedValueCount != fn.returnTypes.size()) {
                diagnostics_.error(ret.range, "multi-return statement must return the same number of values as declared");
            }
            std::unordered_set<std::string> returnedUniqueValues;
            std::size_t returnIndex = 0;
            for (const auto& value : ret.values) {
                validateExpr(*value);
                collectRepeatedUniqueUses(*value, returnedUniqueValues, "unique values cannot be returned more than once from the same statement");
                if (value->kind == ExprKind::DeclRef) {
                    const auto& ref = static_cast<const DeclRefExpr&>(*value);
                    auto it = symbols_.find(ref.name);
                    const bool matchingRefReturn = returnIndex < fn.returnTypes.size() && ownershipFromType(fn.returnTypes[returnIndex]) == ValueInfo::Ownership::Ref;
                    if (it != symbols_.end() && it->second.ownership == ValueInfo::Ownership::Ref && !matchingRefReturn) {
                        diagnostics_.error(value->range, "ref values cannot escape by being returned");
                    }
                }
                returnIndex += exprValueCount(*value);
            }
            break;
        }
        case StmtKind::Defer: {
            const auto& deferStmt = static_cast<const DeferStmt&>(stmt);
            if (!deferStmt.call) {
                diagnostics_.error(deferStmt.range, "defer expects a function or method call");
                break;
            }
            validateExpr(*deferStmt.call);
            requireSingleValue(*deferStmt.call, "defer calls must be single-value expressions");
            if (deferStmt.call->kind != ExprKind::Call) {
                diagnostics_.error(deferStmt.call->range, "defer expects a function or method call");
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
                const auto initializerType = exprType(*letStmt.initializer);
                const auto bindingOwnership = !firstBinding.explicitType.name.empty() ? ownershipFromType(firstBinding.explicitType) : ValueInfo::Ownership::Unknown;
                ValueInfo::Ownership initializerOwnership = value.ownership;
                if (initializerOwnership == ValueInfo::Ownership::Unknown && initializerType.has_value()) {
                    initializerOwnership = ownershipFromType(*initializerType);
                }
                if (letStmt.initializer->kind == ExprKind::NullLiteral && !firstBinding.explicitType.name.empty() &&
                    ownershipFromType(firstBinding.explicitType) == ValueInfo::Ownership::Value) {
                    diagnostics_.error(letStmt.initializer->range, "null cannot initialize a value that is not nullable");
                }
                if (!firstBinding.explicitType.name.empty() && bindingOwnership == ValueInfo::Ownership::Ref &&
                    initializerOwnership != ValueInfo::Ownership::Value && initializerOwnership != ValueInfo::Ownership::Unknown && initializerOwnership != ValueInfo::Ownership::Ref) {
                    diagnostics_.error(letStmt.range, "ref values cannot be stored from owning heap initializers");
                }
                if (!firstBinding.explicitType.name.empty() && bindingOwnership != ValueInfo::Ownership::Ref && initializerOwnership == ValueInfo::Ownership::Ref) {
                    diagnostics_.error(letStmt.range, "ref values cannot be stored in owning or value bindings");
                }
                if (!firstBinding.explicitType.name.empty() && bindingOwnership == ValueInfo::Ownership::Weak &&
                    initializerOwnership != ValueInfo::Ownership::Arc && initializerOwnership != ValueInfo::Ownership::Weak && initializerOwnership != ValueInfo::Ownership::Unknown) {
                    diagnostics_.error(letStmt.range, "weak values must originate from ARC or weak references");
                }
                if (!firstBinding.explicitType.name.empty() && bindingOwnership == ValueInfo::Ownership::Arc && initializerOwnership == ValueInfo::Ownership::Unique) {
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
                        (binding.explicitType.name.empty() ? value.nullable : typeSupportsNullability(binding.explicitType));
                    Type storedSymbolType = bindingType;
                    if (!binding.explicitType.name.empty() && letStmt.initializer->kind == ExprKind::Initializer) {
                        const auto& init = static_cast<const InitializerExpr&>(*letStmt.initializer);
                        if (init.initKind == InitKind::ArrayLiteral && !storedSymbolType.arrayExtents.empty() && !storedSymbolType.arrayExtents.front().has_value()) {
                            storedSymbolType.arrayExtents.front() = init.values.size();
                            symbolTypes_[binding.name] = storedSymbolType;
                        }
                    }
                    if (!binding.explicitType.name.empty() && value.nullable && typeSupportsNullability(binding.explicitType) && storedSymbolType.pointerDepth == 0) {
                        ++storedSymbolType.pointerDepth;
                        symbolTypes_[binding.name] = storedSymbolType;
                    }
                    symbols_[binding.name] = !binding.explicitType.name.empty()
                        ? ValueInfo {ownershipFromType(binding.explicitType), binding.explicitType.name, nullable, letStmt.mutableStorage}
                        : ValueInfo {value.ownership, bindingType.name, nullable, letStmt.mutableStorage};
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
                    symbols_[binding.name] = ValueInfo {ownershipFromType(binding.explicitType), binding.explicitType.name,
                                                        isPointerLike(binding.explicitType), letStmt.mutableStorage};
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
                validateStmt(*ifStmt.thenBlock, fn, loopDepth, switchDepth);
                symbols_ = savedSymbols;
                symbolTypes_ = savedTypes;
                if (ifStmt.elseBranch) {
                    validateStmt(*ifStmt.elseBranch, fn, loopDepth, switchDepth);
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
                        validateStmt(*ifStmt.thenBlock, fn, loopDepth, switchDepth);
                        symbols_ = savedSymbols;
                        symbolTypes_ = savedTypes;
                        auto it = symbols_.find(ref.name);
                        if (it != symbols_.end()) {
                            it->second.nullable = false;
                        }
                        validateStmt(*ifStmt.elseBranch, fn, loopDepth, switchDepth);
                        symbols_ = savedSymbols;
                        symbolTypes_ = savedTypes;
                        break;
                    }
                }
            }
            validateStmt(*ifStmt.thenBlock, fn, loopDepth, switchDepth);
            symbols_ = savedSymbols;
            symbolTypes_ = savedTypes;
            if (ifStmt.elseBranch) {
                validateStmt(*ifStmt.elseBranch, fn, loopDepth, switchDepth);
                symbols_ = savedSymbols;
                symbolTypes_ = savedTypes;
            }
            break;
        }
        case StmtKind::While: {
            const auto& whileStmt = static_cast<const WhileStmt&>(stmt);
            validateExpr(*whileStmt.condition);
            requireSingleValue(*whileStmt.condition, "while conditions must be single values");
            validateStmt(*whileStmt.body, fn, loopDepth + 1, switchDepth);
            break;
        }
        case StmtKind::For: {
            const auto& forStmt = static_cast<const ForStmt&>(stmt);
            const auto savedSymbols = symbols_;
            const auto savedTypes = symbolTypes_;
            if (forStmt.initializer) {
                validateStmt(*forStmt.initializer, fn, loopDepth, switchDepth);
            }
            if (forStmt.condition) {
                validateExpr(*forStmt.condition);
                requireSingleValue(*forStmt.condition, "for conditions must be single values");
            }
            if (forStmt.step) {
                validateExpr(*forStmt.step);
                requireSingleValue(*forStmt.step, "for step expressions must be single values");
            }
            validateStmt(*forStmt.body, fn, loopDepth + 1, switchDepth);
            symbols_ = savedSymbols;
            symbolTypes_ = savedTypes;
            break;
        }
        case StmtKind::DoWhile: {
            const auto& doWhileStmt = static_cast<const DoWhileStmt&>(stmt);
            validateStmt(*doWhileStmt.body, fn, loopDepth + 1, switchDepth);
            validateExpr(*doWhileStmt.condition);
            requireSingleValue(*doWhileStmt.condition, "do-while conditions must be single values");
            break;
        }
        case StmtKind::Switch: {
            const auto& switchStmt = static_cast<const SwitchStmt&>(stmt);
            validateExpr(*switchStmt.condition);
            requireSingleValue(*switchStmt.condition, "switch conditions must be single values");
            bool sawDefault = false;
            std::unordered_map<std::uint64_t, std::string> coveredValues;
            auto conditionType = exprType(*switchStmt.condition);
            for (const auto& switchCase : switchStmt.cases) {
                if (switchCase.isDefault) {
                    if (sawDefault) {
                        diagnostics_.error(switchCase.range, "switch cannot contain more than one default case");
                    }
                    sawDefault = true;
                } else if (switchCase.patterns.empty()) {
                    diagnostics_.error(switchCase.range, "switch case requires at least one value");
                }
                for (const auto& pattern : switchCase.patterns) {
                    validateExpr(*pattern.value);
                    requireSingleValue(*pattern.value, "switch case values must be single values");
                    if (!evalExpr(*pattern.value).has_value()) {
                        diagnostics_.error(pattern.range, "switch case values must be compile-time constants");
                    }

                    if (conditionType.has_value()) {
                        for (const auto& [value, label] : expandSwitchPatternValues(pattern, *conditionType)) {
                            if (auto [it, inserted] = coveredValues.emplace(value, label); !inserted) {
                                diagnostics_.error(pattern.range, "switch case overlaps a previous case: " + label);
                            }
                        }
                    }
                }
                if (switchCase.body) {
                    validateStmt(*switchCase.body, fn, loopDepth, switchDepth + 1);
                }
            }

            if (!sawDefault && conditionType.has_value()) {
                auto enumIt = enumInfos_.find(conditionType->name);
                if (enumIt != enumInfos_.end() && !enumIt->second.isFlags) {
                    const auto missing = missingEnumSwitchCases(switchStmt, *conditionType);
                    if (!missing.empty()) {
                        std::string message = "switch over enum '" + conditionType->name + "' must cover all cases or provide a default";
                        if (missing.size() == 1 && missing.front() == "<non-constant case pattern>") {
                            message += "; exhaustive analysis requires constant enum cases";
                        } else {
                            message += "; missing: ";
                            for (std::size_t i = 0; i < missing.size(); ++i) {
                                if (i != 0) {
                                    message += ", ";
                                }
                                message += missing[i];
                            }
                        }
                        diagnostics_.error(switchStmt.range, message);
                    }
                }
            }
            break;
        }
        case StmtKind::Break:
            if (loopDepth == 0 && switchDepth == 0) {
                diagnostics_.error(stmt.range, "break can only appear inside loops or switch cases");
            }
            break;
        case StmtKind::Continue:
            if (loopDepth == 0) {
                diagnostics_.error(stmt.range, "continue can only appear inside loops");
            }
            break;
    }
}

}  // namespace axc
