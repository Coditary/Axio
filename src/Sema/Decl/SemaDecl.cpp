/// @file
/// @brief Declaration and statement semantic validation.

#include "../Internal/SemaInternal.h"

#include <algorithm>

#include "axc/Support/Diagnostic.h"

namespace axc {

namespace {

bool sameType(const Type& lhs, const Type& rhs) {
    return lhs.name == rhs.name && lhs.pointerDepth == rhs.pointerDepth && lhs.arrayExtents == rhs.arrayExtents;
}

bool compatibleArrayType(const Type& declaredType, const Type& actualType) {
    if (declaredType.name != actualType.name || declaredType.pointerDepth != actualType.pointerDepth) {
        return false;
    }
    if (declaredType.arrayExtents.size() != actualType.arrayExtents.size()) {
        return false;
    }
    for (std::size_t i = 0; i < declaredType.arrayExtents.size(); ++i) {
        if (!declaredType.arrayExtents[i].has_value()) {
            continue;
        }
        if (declaredType.arrayExtents[i] != actualType.arrayExtents[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::string> SemaImpl::missingEnumSwitchCases(const SwitchStmt& stmt, const Type& conditionType) const {
    std::vector<std::string> missing;
    auto enumIt = enumInfos_.find(conditionType.name);
    if (enumIt == enumInfos_.end()) {
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
        orderedValues.emplace_back(value, name);
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
        if (enumIt != enumInfos_.end()) {
            for (const auto& [name, ordinal] : enumIt->second.values) {
                if (ordinal == value) {
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
        case DeclKind::Struct:
        case DeclKind::Enum:
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
    }
}

void SemaImpl::validateGlobal(const GlobalVarDecl& decl) {
    if (!decl.type.name.empty()) {
        globalSymbolTypes_[decl.name] = decl.type;
        globalSymbols_[decl.name] = ValueInfo {ownershipFromType(decl.type), decl.type.name, decl.mutableStorage};
    }
    if (decl.type.name.empty() && !decl.initializer) {
        diagnostics_.error(decl.range, "global declaration needs either a type or an initializer");
        return;
    }
    if (!decl.initializer) {
        return;
    }

    validateExpr(*decl.initializer);
    requireSingleValue(*decl.initializer, "global initializers must evaluate to a single value");
    if (!decl.type.name.empty()) {
        auto initType = exprType(*decl.initializer);
        if (initType.has_value() && !isAssignableType(decl.type, *initType) && !compatibleArrayType(decl.type, *initType)) {
            diagnostics_.error(decl.initializer->range, "global initializer type does not match declared type");
        }
    }
}

void SemaImpl::validateClass(const ClassDecl& decl) {
    std::unordered_set<std::string> memberNames;
    std::unordered_set<std::string> methodNames;
    for (const auto& member : decl.members) {
        if (!memberNames.insert(member.name).second) {
            diagnostics_.error(member.range, "duplicate class member '" + member.name + "'");
        }
        if (member.type.name.empty()) {
            diagnostics_.error(member.range, "class members need a declared type");
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

    if (!fn.receiverType.empty()) {
        auto classIt = classInfos_.find(fn.receiverType);
        if (classIt != classInfos_.end()) {
            for (const auto& [name, type] : classIt->second.fieldTypes) {
                symbols_[name] = ValueInfo {ownershipFromType(type), type.name, false};
                symbolTypes_[name] = type;
            }
        }
    }

    for (const auto& param : fn.parameters) {
        symbols_[param.name] = ValueInfo {ownershipFromType(param.type), param.type.name, !param.isConst};
        symbolTypes_[param.name] = param.type;
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
            if (fn.returnsVoid()) {
                if (ret.value) {
                    diagnostics_.error(ret.range, "void functions cannot return a value");
                }
                break;
            }
            if (!ret.value) {
                diagnostics_.error(ret.range, "non-void functions must return a value");
                break;
            }
            validateExpr(*ret.value);
            requireSingleValue(*ret.value, "return expressions must be single values");
            if (fn.returnType.has_value()) {
                auto returnExprType = exprType(*ret.value);
                if (returnExprType.has_value() && !isAssignableType(*fn.returnType, *returnExprType)) {
                    diagnostics_.error(ret.value->range, "return type does not match function signature");
                }
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
            if (letStmt.bindings.size() > 1) {
                diagnostics_.error(letStmt.range, "multiple let bindings are not supported in the MVP");
                break;
            }
            const LetBinding& binding = letStmt.bindings.front();
            if (binding.explicitType.name.empty() && !letStmt.initializer) {
                diagnostics_.error(letStmt.range, "let declaration needs either a type or an initializer");
                break;
            }
            Type bindingType = binding.explicitType;
            if (letStmt.initializer) {
                validateExpr(*letStmt.initializer);
                auto initType = exprType(*letStmt.initializer);
                if (bindingType.name.empty() && initType.has_value()) {
                    bindingType = *initType;
                }
                if (!binding.explicitType.name.empty() && initType.has_value() &&
                    !isAssignableType(binding.explicitType, *initType) && !compatibleArrayType(binding.explicitType, *initType)) {
                    diagnostics_.error(letStmt.initializer->range, "initializer type does not match declared type");
                }
                if (!binding.explicitType.name.empty() && letStmt.initializer->kind == ExprKind::Initializer) {
                    const auto& init = static_cast<const InitializerExpr&>(*letStmt.initializer);
                    if (init.initKind == InitKind::ArrayLiteral && !bindingType.arrayExtents.empty() && !bindingType.arrayExtents.front().has_value()) {
                        bindingType.arrayExtents.front() = init.values.size();
                    }
                }
            }
            if (bindingType.name.empty()) {
                diagnostics_.error(binding.range, "could not infer type for let binding '" + binding.name + "'");
                break;
            }
            symbolTypes_[binding.name] = bindingType;
            symbols_[binding.name] = ValueInfo {ownershipFromType(bindingType), bindingType.name, letStmt.mutableStorage};
            break;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            validateExpr(*ifStmt.condition);
            requireSingleValue(*ifStmt.condition, "if conditions must be single values");
            const auto savedSymbols = symbols_;
            const auto savedTypes = symbolTypes_;
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
                if (enumIt != enumInfos_.end()) {
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
