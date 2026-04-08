#include "../Internal/SemaInternal.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/QualifiedName.h"

namespace axc {

namespace {

std::string loweredFunctionName(const FunctionDecl& fn) {
    return fn.receiverType.empty() ? fn.name : fn.receiverType + "." + fn.name;
}

bool sameType(const Type& lhs, const Type& rhs) {
    return lhs.name == rhs.name && lhs.pointerDepth == rhs.pointerDepth && lhs.arrayExtents == rhs.arrayExtents;
}

bool isIntegerScalarName(const std::string& name) {
    return name == "int" || name == "i2" || name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
           name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "char";
}

bool isFloatingScalarName(const std::string& name) {
    return name == "float" || name == "f16" || name == "f32" || name == "double" || name == "f64";
}

}  // namespace

SemaImpl::SemaImpl(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool SemaImpl::analyze(TranslationUnit& translationUnit) {
    enumInfos_.clear();
    classInfos_.clear();
    globalSymbols_.clear();
    globalSymbolTypes_.clear();
    symbols_.clear();
    symbolTypes_.clear();
    structFields_.clear();
    structFieldTypes_.clear();
    functionArgumentCount_.clear();
    functionReturnTypes_.clear();

    buildEnumTables(translationUnit);
    buildClassTables(translationUnit);
    recordFunctionSignatures(translationUnit);

    for (const auto& decl : translationUnit.declarations) {
        validateDecl(*decl);
    }

    return !diagnostics_.hasErrors();
}

ValueInfo::Ownership SemaImpl::ownershipFromType(const Type& type) const {
    (void)type;
    return ValueInfo::Ownership::Value;
}

bool SemaImpl::isPointerLike(const Type& type) const {
    return type.pointerDepth > 0 || type.name == "str" || !type.arrayExtents.empty();
}

bool SemaImpl::isAssignableType(const Type& target, const Type& actual) const {
    if (sameType(target, actual)) {
        return true;
    }

    if (target.pointerDepth != actual.pointerDepth || target.arrayExtents != actual.arrayExtents) {
        return false;
    }

    if (target.name == "bool" && (actual.name == "bool" || isIntegerScalarName(actual.name))) {
        return true;
    }
    if (isIntegerScalarName(target.name) && (isIntegerScalarName(actual.name) || actual.name == "bool")) {
        return true;
    }
    if (isFloatingScalarName(target.name) && (isFloatingScalarName(actual.name) || isIntegerScalarName(actual.name) || actual.name == "bool")) {
        return true;
    }

    return false;
}

std::optional<Type> SemaImpl::exprType(const Expr& expr) const {
    switch (expr.kind) {
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto it = symbolTypes_.find(ref.name);
            if (it != symbolTypes_.end()) {
                return it->second;
            }
            for (const auto& [enumName, info] : enumInfos_) {
                if (info.values.contains(ref.name)) {
                    Type type;
                    type.name = enumName;
                    type.range = expr.range;
                    return type;
                }
            }
            return std::nullopt;
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
            type.name = "double";
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
            return type;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len" && call.arguments.size() == 1) {
                    Type type;
                    type.name = "int";
                    type.range = expr.range;
                    return type;
                }
            }
            if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto it = functionReturnTypes_.find(*calleeName);
                if (it != functionReturnTypes_.end()) {
                    return it->second;
                }
            }
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (auto baseType = exprType(*member.base); baseType.has_value()) {
                    auto it = functionReturnTypes_.find(baseType->name + "." + member.member);
                    if (it != functionReturnTypes_.end()) {
                        return it->second;
                    }
                }
            }
            return std::nullopt;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            auto operandType = exprType(*unary.operand);
            if (!operandType.has_value()) {
                return std::nullopt;
            }
            switch (unary.op) {
                case UnaryOp::AddressOf:
                    ++operandType->pointerDepth;
                    operandType->range = expr.range;
                    return operandType;
                case UnaryOp::Dereference:
                    if (operandType->pointerDepth > 0) {
                        --operandType->pointerDepth;
                    }
                    operandType->range = expr.range;
                    return operandType;
                default:
                    operandType->range = expr.range;
                    return operandType;
            }
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            if (binary.op == BinaryOp::Assign) {
                return exprType(*binary.lhs);
            }
            switch (binary.op) {
                case BinaryOp::Equal:
                case BinaryOp::NotEqual:
                case BinaryOp::Less:
                case BinaryOp::LessEqual:
                case BinaryOp::Greater:
                case BinaryOp::GreaterEqual:
                case BinaryOp::LogicalAnd:
                case BinaryOp::LogicalOr: {
                    Type type;
                    type.name = "bool";
                    type.range = expr.range;
                    return type;
                }
                default:
                    return exprType(*binary.lhs);
            }
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            if (auto baseType = exprType(*member.base); baseType.has_value()) {
                auto classIt = classInfos_.find(baseType->name);
                if (classIt != classInfos_.end()) {
                    auto fieldIt = classIt->second.fieldTypes.find(member.member);
                    if (fieldIt != classIt->second.fieldTypes.end()) {
                        return fieldIt->second;
                    }
                }
                auto structIt = structFieldTypes_.find(baseType->name);
                if (structIt != structFieldTypes_.end()) {
                    auto fieldIt = structIt->second.find(member.member);
                    if (fieldIt != structIt->second.end()) {
                        return fieldIt->second;
                    }
                }
            }
            if (auto qualifiedName = qualifiedNameFromExpr(expr); qualifiedName.has_value()) {
                for (const auto& [enumName, info] : enumInfos_) {
                    const std::string prefix = enumName + ".";
                    if (qualifiedName->rfind(prefix, 0) == 0) {
                        const std::string suffix = qualifiedName->substr(prefix.size());
                        if (info.values.contains(suffix)) {
                            Type type;
                            type.name = enumName;
                            type.range = expr.range;
                            return type;
                        }
                    }
                }
            }
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::string> SemaImpl::moduleQualifiedName(const Expr& expr) const {
    auto name = qualifiedNameFromExpr(expr);
    if (!name.has_value()) {
        return std::nullopt;
    }
    const std::size_t split = name->find('.');
    if (split == std::string::npos) {
        return name;
    }
    const std::string root = name->substr(0, split);
    if (symbols_.contains(root) || globalSymbols_.contains(root)) {
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
    if (symbols_.contains(name) || globalSymbols_.contains(name) || functionArgumentCount_.contains(name) || enumInfos_.contains(name) ||
        classInfos_.contains(name) || structFields_.contains(name)) {
        return true;
    }
    for (const auto& [_, info] : enumInfos_) {
        if (info.values.contains(name)) {
            return true;
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
                globalSymbols_[global.name] = ValueInfo {ownershipFromType(globalType), globalType.name, global.mutableStorage};
            }
            continue;
        }

        if (decl->kind != DeclKind::Function) {
            continue;
        }

        const auto& fn = static_cast<const FunctionDecl&>(*decl);
        const std::string loweredName = loweredFunctionName(fn);
        functionArgumentCount_[loweredName] = fn.receiverType.empty() ? fn.parameters.size() : (fn.parameters.empty() ? 0U : fn.parameters.size() - 1U);
        if (fn.returnType.has_value()) {
            functionReturnTypes_[loweredName] = *fn.returnType;
        }
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
        for (const auto& member : classDecl.members) {
            info.fields.insert(member.name);
            if (!member.type.name.empty()) {
                info.fieldTypes[member.name] = member.type;
            }
        }
        for (const auto& method : classDecl.methods) {
            info.methods.insert(method->name);
            const auto& fn = static_cast<const FunctionDecl&>(*method);
            info.methodArgumentCounts[method->name] = fn.receiverType.empty() ? fn.parameters.size() : (fn.parameters.empty() ? 0U : fn.parameters.size() - 1U);
        }
        classInfos_[classDecl.name] = std::move(info);
    }
}

ValueInfo SemaImpl::inferExpr(const Expr& expr) const {
    auto type = exprType(expr);
    if (type.has_value()) {
        return ValueInfo {ownershipFromType(*type), type->name, true};
    }
    return ValueInfo {};
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
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (containsDeclRef(*call.callee, name)) {
                return true;
            }
            for (const auto& arg : call.arguments) {
                if (containsDeclRef(*arg, name)) {
                    return true;
                }
            }
            return false;
        }
        case ExprKind::Member:
            return containsDeclRef(*static_cast<const MemberExpr&>(expr).base, name);
        case ExprKind::Initializer:
            for (const auto& value : static_cast<const InitializerExpr&>(expr).values) {
                if (containsDeclRef(*value, name)) {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

void SemaImpl::requireSingleValue(const Expr& expr, const std::string& message) {
    (void)expr;
    (void)message;
}

void SemaImpl::buildEnumTables(TranslationUnit& translationUnit) {
    for (const auto& decl : translationUnit.declarations) {
        if (decl->kind != DeclKind::Enum) {
            continue;
        }

        const auto& enumDecl = static_cast<const EnumDecl&>(*decl);
        EnumInfo info;
        std::uint64_t nextValue = 0;
        for (const auto& element : enumDecl.elements) {
            const std::uint64_t assigned = nextValue++;
            info.values[element.name] = assigned;
            info.maxOrdinal = assigned;
        }
        enumInfos_[enumDecl.name] = std::move(info);
    }
}

}  // namespace axc
