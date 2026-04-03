#include "LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

ModuleEmitter::ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics), module_(std::make_unique<llvm::Module>("axio_module", context_)), builder_(context_) {}

void ModuleEmitter::declareRuntimeFunctions() {
    llvm::Type* voidPtrTy = llvm::PointerType::get(context_, 0);
    llvm::Type* i64Ty = llvm::Type::getInt64Ty(context_);
    llvm::Type* voidTy = llvm::Type::getVoidTy(context_);

    runtime_.arcAlloc = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_arc_alloc", voidPtrTy, i64Ty).getCallee());
    runtime_.arcRetain = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_arc_retain", voidPtrTy, voidPtrTy).getCallee());
    runtime_.arcRelease = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_arc_release", voidTy, voidPtrTy).getCallee());
    runtime_.weakInit = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_weak_init", voidPtrTy, voidPtrTy).getCallee());
    runtime_.weakRelease = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_weak_release", voidTy, voidPtrTy).getCallee());
    runtime_.weakLoad = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_weak_load", voidPtrTy, voidPtrTy).getCallee());
    runtime_.arcStrongCount = llvm::cast<llvm::Function>(module_->getOrInsertFunction("axio_arc_strong_count", i64Ty, voidPtrTy).getCallee());
}

bool ModuleEmitter::isArcOwnedType(const Type& type) const {
    if (type.name == "str" || type.name == "error") {
        return false;
    }
    if (!isClassType(type)) {
        return false;
    }
    for (TypeModifier modifier : type.modifiers) {
        if (modifier == TypeModifier::Ref || modifier == TypeModifier::Weak || modifier == TypeModifier::Unique) {
            return false;
        }
    }
    return structTypes_.contains(type.name);
}

bool ModuleEmitter::isWeakType(const Type& type) const {
    return std::find(type.modifiers.begin(), type.modifiers.end(), TypeModifier::Weak) != type.modifiers.end();
}

bool ModuleEmitter::isUniqueType(const Type& type) const {
    return std::find(type.modifiers.begin(), type.modifiers.end(), TypeModifier::Unique) != type.modifiers.end();
}

bool ModuleEmitter::isClassType(const Type& type) const {
    return classFieldNames_.contains(type.name) || classMethodNames_.contains(type.name);
}

llvm::Value* ModuleEmitter::retainForStorage(llvm::Value* value, const Type& type) {
    if (value == nullptr || !value->getType()->isPointerTy()) {
        return value;
    }
    llvm::Value* asVoidPtr = builder_.CreatePointerCast(value, llvm::PointerType::get(context_, 0), "retain.cast");
    if (isWeakType(type)) {
        llvm::Value* weakValue = builder_.CreateCall(runtime_.weakInit, {asVoidPtr}, "weak.init");
        return builder_.CreatePointerCast(weakValue, value->getType(), "weak.cast");
    }
    if (isArcOwnedType(type)) {
        llvm::Value* retained = builder_.CreateCall(runtime_.arcRetain, {asVoidPtr}, "arc.retain");
        return builder_.CreatePointerCast(retained, value->getType(), "arc.cast");
    }
    return value;
}

bool ModuleEmitter::shouldRetainForStorage(const Expr& expr, const Type& type) const {
    if (!isArcOwnedType(type) && !isWeakType(type)) {
        return false;
    }
    switch (expr.kind) {
        case ExprKind::DeclRef:
            return true;
        case ExprKind::NullLiteral:
        case ExprKind::Initializer:
        case ExprKind::Call:
            return false;
        default:
            return true;
    }
}

void ModuleEmitter::releaseStoredValue(llvm::Value* address, const Type& type) {
    if (address == nullptr) {
        return;
    }
    llvm::Type* llvmType = lowerType(type);
    if (!llvmType->isPointerTy()) {
        return;
    }
    llvm::Value* current = builder_.CreateLoad(llvmType, address, "release.load");
    llvm::Value* asVoidPtr = builder_.CreatePointerCast(current, llvm::PointerType::get(context_, 0), "release.cast");
    if (isWeakType(type)) {
        builder_.CreateCall(runtime_.weakRelease, {asVoidPtr});
        return;
    }
    if (isArcOwnedType(type)) {
        builder_.CreateCall(runtime_.arcRelease, {asVoidPtr});
    }
}

void ModuleEmitter::releaseLocals() {
    for (const auto& [_, symbol] : locals_) {
        releaseStoredValue(symbol.address, symbol.type);
    }
}

std::unique_ptr<llvm::Module> ModuleEmitter::emit(const TranslationUnit& translationUnit) {
    module_->setSourceFileName(sourceManager_.path().string());
    declareRuntimeFunctions();

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            classFieldNames_[classDecl.name];
            classMethodNames_[classDecl.name];
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
        } else if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            for (const auto& method : classDecl.methods) {
                declareFunction(static_cast<const FunctionDecl&>(*method));
            }
        }
    }

    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Import) {
            continue;
        }
        if (declaration->kind == DeclKind::Function) {
            defineFunction(*static_cast<const FunctionDecl*>(declaration.get()));
        } else if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            for (const auto& method : classDecl.methods) {
                defineFunction(static_cast<const FunctionDecl&>(*method));
            }
        }
    }

    return std::move(module_);
}

void ModuleEmitter::collectEnum(const EnumDecl& declaration) {
    EnumValueInfo info;
    info.isFlags = declaration.isFlags;
    std::uint64_t nextValue = 0;
    std::uint64_t nextFlagBit = 0;
    for (const auto& element : declaration.elements) {
        const std::uint64_t assigned = element.constantValue.has_value()
            ? *element.constantValue
            : (declaration.isFlags ? (1ULL << nextFlagBit) : nextValue);
        info.values[element.name] = assigned;
        if (!declaration.isFlags) {
            info.maxOrdinal = assigned > info.maxOrdinal ? assigned : info.maxOrdinal;
            ++nextValue;
        } else {
            ++nextFlagBit;
        }
        if (!declaration.parameters.empty() && element.payloadValues.size() == declaration.parameters.size()) {
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                if (const auto* intLiteral = dynamic_cast<const IntegerLiteralExpr*>(element.payloadValues[i].get())) {
                    info.paramValues[element.name][declaration.parameters[i].name] = static_cast<std::uint64_t>(intLiteral->value);
                }
            }
        }
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
    enumValues_[declaration.name] = std::move(info);
}

bool ModuleEmitter::isUnsignedType(const Type& type) const {
    return type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64";
}

bool ModuleEmitter::isNullableStorageType(const Type& type) const {
    if (type.name == "str" || type.name == "error") {
        return true;
    }
    if (type.pointerDepth > 0) {
        return true;
    }
    for (TypeModifier modifier : type.modifiers) {
        if (modifier == TypeModifier::Ref || modifier == TypeModifier::Weak || modifier == TypeModifier::Unique) {
            return true;
        }
    }
    return isClassType(type);
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
        if (value->getType()->isIntegerTy(1)) {
            return builder_.CreateZExt(value, target, "boolzext");
        }
        if (target->isIntegerTy(1)) {
            return builder_.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "inttobool");
        }
        return builder_.CreateIntCast(value, target, !isUnsignedType(targetType), "intcast");
    }
    if (target->isPointerTy() && value->getType()->isIntegerTy(1)) {
        return builder_.CreateIntToPtr(builder_.CreateZExt(value, llvm::Type::getInt64Ty(context_), "booltoint"), target, "booltoptr");
    }
    if (target->isFloatingPointTy() && value->getType()->isFloatingPointTy()) {
        return builder_.CreateFPCast(value, target, "fpcast");
    }
    if (target->isFloatingPointTy() && value->getType()->isIntegerTy()) {
        return isUnsignedType(targetType) ? builder_.CreateUIToFP(value, target, "uitofp") : builder_.CreateSIToFP(value, target, "sitofp");
    }
    if (target->isIntegerTy(1) && value->getType()->isPointerTy()) {
        return builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "ptrtobool");
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
    for (TypeModifier modifier : type.modifiers) {
        if (modifier == TypeModifier::Ref || modifier == TypeModifier::Weak || modifier == TypeModifier::Unique) {
            lowered = llvm::PointerType::get(context_, 0);
        }
    }
    return lowered;
}

bool ModuleEmitter::isLowerableFunction(const FunctionDecl& declaration) {
    (void)declaration;
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
        if (isArcOwnedType(declaration.returnTypes.front())) {
            llvm::Type* lowered = lowerType(declaration.returnTypes.front());
            return llvm::PointerType::get(context_, 0);
        }
        return lowerType(declaration.returnTypes.front());
    }

    std::vector<llvm::Type*> elementTypes;
    elementTypes.reserve(declaration.returnTypes.size());
    for (const auto& type : declaration.returnTypes) {
        if (isArcOwnedType(type)) {
            elementTypes.push_back(llvm::PointerType::get(context_, 0));
        } else {
            elementTypes.push_back(lowerType(type));
        }
    }
    return llvm::StructType::get(context_, elementTypes, false);
}

Type ModuleEmitter::inferExprType(const Expr& expr, std::size_t valueIndex) const {
    switch (expr.kind) {
        case ExprKind::Initializer: {
            Type type;
            type.name = static_cast<const InitializerExpr&>(expr).typeName;
            type.range = expr.range;
            if (isClassType(type) || static_cast<const InitializerExpr&>(expr).initKind != InitKind::Value) {
                ++type.pointerDepth;
            }
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
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            if (auto className = inferClassTypeName(*member.base); className.has_value()) {
                auto layoutIt = aggregateLayouts_.find(*className);
                if (layoutIt != aggregateLayouts_.end()) {
                    auto fieldIt = layoutIt->second.fieldIndices.find(member.member);
                    if (fieldIt != layoutIt->second.fieldIndices.end()) {
                        return layoutIt->second.fieldTypes[fieldIt->second];
                    }
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
            Type operandType = inferExprType(*unary.operand);
            if (unary.op == UnaryOp::AddressOf) {
                ++operandType.pointerDepth;
                operandType.range = expr.range;
                return operandType;
            }
            if (unary.op == UnaryOp::Dereference) {
                if (operandType.pointerDepth > 0) {
                    --operandType.pointerDepth;
                } else if (!operandType.modifiers.empty()) {
                    operandType.modifiers.erase(operandType.modifiers.begin());
                }
                operandType.range = expr.range;
                return operandType;
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
    if (isClassType(type)) {
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
