#include "LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

ModuleEmitter::ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics), module_(std::make_unique<llvm::Module>("axio_module", context_)), builder_(context_) {}

std::unique_ptr<llvm::Module> ModuleEmitter::emit(const TranslationUnit& translationUnit) {
    module_->setSourceFileName(sourceManager_.path().string());

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            for (const auto& member : classDecl.members) {
                classFieldNames_[classDecl.name].insert(member.name);
            }
            for (const auto& method : classDecl.methods) {
                classMethodNames_[classDecl.name].insert(method->name);
            }
        }
    }

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Enum) {
            collectEnum(*static_cast<const EnumDecl*>(declaration.get()));
        }
    }

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Struct) {
            declareStruct(*static_cast<const StructDecl*>(declaration.get()));
        }
        if (declaration->kind == DeclKind::Class) {
            declareClass(*static_cast<const ClassDecl*>(declaration.get()));
        }
    }

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Function) {
            declareFunction(*static_cast<const FunctionDecl*>(declaration.get()));
        }
    }

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Function) {
            defineFunction(*static_cast<const FunctionDecl*>(declaration.get()));
        }
    }

    return std::move(module_);
}

void ModuleEmitter::collectEnum(const EnumDecl& declaration) {
    EnumValueInfo info;
    info.isFlags = declaration.isFlags;
    for (const auto& element : declaration.elements) {
        if (element.constantValue.has_value()) {
            info.values[element.name] = *element.constantValue;
            if (!declaration.isFlags) {
                info.maxOrdinal = *element.constantValue > info.maxOrdinal ? *element.constantValue : info.maxOrdinal;
            }
        }
        if (!declaration.parameters.empty() && element.payloadValues.size() == declaration.parameters.size()) {
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                if (const auto* intLiteral = dynamic_cast<const IntegerLiteralExpr*>(element.payloadValues[i].get())) {
                    info.paramValues[element.name][declaration.parameters[i].name] = static_cast<std::uint64_t>(intLiteral->value);
                }
            }
        }
        for (const auto& nestedDecl : element.nestedDecls) {
            if (nestedDecl->kind != DeclKind::Enum) {
                continue;
            }
            const auto& nestedEnum = static_cast<const EnumDecl&>(*nestedDecl);
            for (const auto& nestedElement : nestedEnum.elements) {
                if (nestedElement.constantValue.has_value()) {
                    info.values[element.name + "." + nestedElement.name] = *nestedElement.constantValue;
                }
            }
        }
    }
    enumValues_[declaration.name] = std::move(info);
}

bool ModuleEmitter::isUnsignedType(const Type& type) const {
    return type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64";
}

llvm::Value* ModuleEmitter::castValueToType(llvm::Value* value, const Type& targetType) {
    if (value == nullptr) {
        return nullptr;
    }

    llvm::Type* target = lowerType(targetType);
    if (value->getType() == target) {
        return value;
    }

    if (target->isIntegerTy() && value->getType()->isIntegerTy()) {
        return builder_.CreateIntCast(value, target, !isUnsignedType(targetType), "intcast");
    }
    if (target->isFloatingPointTy() && value->getType()->isFloatingPointTy()) {
        return builder_.CreateFPCast(value, target, "fpcast");
    }
    if (target->isFloatingPointTy() && value->getType()->isIntegerTy()) {
        return isUnsignedType(targetType) ? builder_.CreateUIToFP(value, target, "uitofp") : builder_.CreateSIToFP(value, target, "sitofp");
    }
    if (target->isIntegerTy() && value->getType()->isFloatingPointTy()) {
        return isUnsignedType(targetType) ? builder_.CreateFPToUI(value, target, "fptoui") : builder_.CreateFPToSI(value, target, "fptosi");
    }
    if (target->isPointerTy() && value->getType()->isPointerTy()) {
        return builder_.CreatePointerCast(value, target, "ptrcast");
    }

    return value;
}

llvm::Type* ModuleEmitter::lowerType(const Type& type) {
    llvm::Type* lowered = nullptr;

    if (type.name.empty() || type.name == "int" || type.name == "error" || type.name == "i32") {
        lowered = llvm::Type::getInt32Ty(context_);
    } else if (type.name == "i2") {
        lowered = llvm::Type::getIntNTy(context_, 2);
    } else if (type.name == "i8") {
        lowered = llvm::Type::getInt8Ty(context_);
    } else if (type.name == "i16" || type.name == "short") {
        lowered = llvm::Type::getInt16Ty(context_);
    } else if (type.name == "i64" || type.name == "long") {
        lowered = llvm::Type::getInt64Ty(context_);
    } else if (type.name == "u8") {
        lowered = llvm::Type::getInt8Ty(context_);
    } else if (type.name == "u16") {
        lowered = llvm::Type::getInt16Ty(context_);
    } else if (type.name == "u32") {
        lowered = llvm::Type::getInt32Ty(context_);
    } else if (type.name == "u64") {
        lowered = llvm::Type::getInt64Ty(context_);
    } else if (type.name == "void") {
        lowered = llvm::Type::getVoidTy(context_);
    } else if (type.name == "bool") {
        lowered = llvm::Type::getInt1Ty(context_);
    } else if (type.name == "char") {
        lowered = llvm::Type::getInt8Ty(context_);
    } else if (type.name == "float" || type.name == "f32") {
        lowered = llvm::Type::getFloatTy(context_);
    } else if (type.name == "double" || type.name == "f64") {
        lowered = llvm::Type::getDoubleTy(context_);
    } else if (type.name == "f16") {
        lowered = llvm::Type::getHalfTy(context_);
    } else if (type.name == "f8") {
        lowered = llvm::Type::getInt8Ty(context_);
    } else if (type.name == "str") {
        lowered = llvm::PointerType::get(context_, 0);
    } else if (enumValues_.contains(type.name)) {
        lowered = llvm::Type::getInt32Ty(context_);
    } else {
        auto it = structTypes_.find(type.name);
        if (it != structTypes_.end()) {
            lowered = it->second;
        } else {
            diagnostics_.error(type.range, "unknown type '" + type.name + "'");
            lowered = llvm::Type::getInt32Ty(context_);
        }
    }

    if (type.name == "str") {
        return lowered;
    }

    for (auto it = type.arrayExtents.rbegin(); it != type.arrayExtents.rend(); ++it) {
        if (it->has_value()) {
            lowered = llvm::ArrayType::get(lowered, **it);
        } else {
            lowered = llvm::PointerType::get(context_, 0);
        }
    }

    for (std::size_t i = 0; i < type.pointerDepth; ++i) {
        lowered = llvm::PointerType::get(context_, 0);
    }
    return lowered;
}

bool ModuleEmitter::isLowerableFunction(const FunctionDecl& declaration) {
    if (!declaration.compileParameters.empty()) {
        diagnostics_.warning(declaration.range, "skipping LLVM emission for function with compile-time parameters: '" + declaration.name + "'");
        return false;
    }
    for (const auto& param : declaration.runtimeParameters) {
        if (!param.type.modifiers.empty()) {
            diagnostics_.warning(param.range, "ownership modifiers are parsed but not yet lowered in codegen");
        }
    }
    return true;
}

Type ModuleEmitter::functionReturnType(const FunctionDecl& declaration) {
    if (declaration.returnTypes.empty()) {
        Type type;
        type.name = "void";
        type.range = declaration.range;
        return type;
    }
    return declaration.returnTypes.front();
}

llvm::Type* ModuleEmitter::lowerFunctionReturnType(const FunctionDecl& declaration) {
    if (declaration.returnTypes.empty()) {
        return llvm::Type::getVoidTy(context_);
    }
    if (declaration.returnTypes.size() == 1) {
        return lowerType(declaration.returnTypes.front());
    }

    std::vector<llvm::Type*> elementTypes;
    elementTypes.reserve(declaration.returnTypes.size());
    for (const auto& type : declaration.returnTypes) {
        elementTypes.push_back(lowerType(type));
    }
    return llvm::StructType::get(context_, elementTypes, false);
}

Type ModuleEmitter::inferExprType(const Expr& expr, std::size_t valueIndex) const {
    switch (expr.kind) {
        case ExprKind::Initializer: {
            Type type;
            type.name = static_cast<const InitializerExpr&>(expr).typeName;
            type.range = expr.range;
            return type;
        }
        case ExprKind::StringLiteral: {
            Type type;
            type.name = "str";
            type.range = expr.range;
            return type;
        }
        case ExprKind::FloatLiteral: {
            Type type;
            type.name = "f64";
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
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto symbol = lookupSymbol(ref.name);
            if (symbol.has_value()) {
                return symbol->type;
            }
            break;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (auto calleeName = moduleQualifiedName(*call.callee); calleeName.has_value()) {
                auto it = functionDecls_.find(*calleeName);
                if (it != functionDecls_.end()) {
                    const FunctionDecl* declaration = it->second;
                    if (valueIndex < declaration->returnTypes.size()) {
                        return declaration->returnTypes[valueIndex];
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    Type fallback;
    fallback.name = "int";
    fallback.range = expr.range;
    return fallback;
}

std::optional<std::string> ModuleEmitter::inferClassTypeName(const Expr& expr) const {
    Type type = inferExprType(expr);
    if (classMethodNames_.contains(type.name) || classFieldNames_.contains(type.name)) {
        return type.name;
    }
    return std::nullopt;
}

std::optional<std::string> ModuleEmitter::moduleQualifiedName(const Expr& expr) const {
    auto name = qualifiedNameFromExpr(expr);
    if (!name.has_value()) {
        return std::nullopt;
    }
    const std::string root = name->substr(0, name->find('.'));
    if (locals_.contains(root)) {
        return std::nullopt;
    }
    return name;
}

}  // namespace axc
