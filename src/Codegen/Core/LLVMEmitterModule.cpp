#include "../Internal/LLVMEmitterInternal.h"

#include "ModuleEmissionWorkflow.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

ModuleEmitter::ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics), module_(std::make_unique<llvm::Module>("axio_module", context_)), builder_(context_) {}

std::unique_ptr<llvm::Module> ModuleEmitter::emit(const TranslationUnit& translationUnit) {
    return detail::ModuleEmissionWorkflow(*this).emit(translationUnit);
}

void ModuleEmitter::collectEnum(const EnumDecl& declaration) {
    EnumValueInfo info;
    std::uint64_t nextValue = 0;
    for (const auto& element : declaration.elements) {
        info.values[element.name] = nextValue;
        info.maxOrdinal = nextValue;
        ++nextValue;
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
        if (value->getType()->isIntegerTy(1)) {
            return builder_.CreateZExt(value, target, "boolzext");
        }
        if (target->isIntegerTy(1)) {
            return builder_.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "inttobool");
        }
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
    if (target->isIntegerTy(1) && value->getType()->isPointerTy()) {
        return builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "ptrtobool");
    }
    if (target->isPointerTy() && value->getType()->isPointerTy()) {
        return builder_.CreatePointerCast(value, target, "ptrcast");
    }

    return value;
}

Type ModuleEmitter::globalStorageType(const GlobalVarDecl& declaration) const {
    if (!declaration.type.name.empty()) {
        return declaration.type;
    }
    if (declaration.initializer) {
        return inferExprType(*declaration.initializer);
    }
    Type fallback;
    fallback.name = "int";
    fallback.range = declaration.range;
    return fallback;
}

llvm::Constant* ModuleEmitter::lowerConstantExpr(const Expr& expr, const Type& targetType) {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral:
            return llvm::dyn_cast<llvm::Constant>(castValueToType(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                                                                          static_cast<const IntegerLiteralExpr&>(expr).value,
                                                                                          true),
                                                                  targetType));
        case ExprKind::BoolLiteral:
            return llvm::dyn_cast<llvm::Constant>(castValueToType(llvm::ConstantInt::getBool(context_, static_cast<const BoolLiteralExpr&>(expr).value),
                                                                  targetType));
        case ExprKind::CharLiteral:
            return llvm::dyn_cast<llvm::Constant>(castValueToType(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_),
                                                                                          static_cast<unsigned char>(static_cast<const CharLiteralExpr&>(expr).value),
                                                                                          false),
                                                                  targetType));
        case ExprKind::FloatLiteral:
            return llvm::dyn_cast<llvm::Constant>(castValueToType(llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_),
                                                                                         static_cast<const FloatLiteralExpr&>(expr).value),
                                                                  targetType));
        default:
            return nullptr;
    }
}

llvm::Type* ModuleEmitter::lowerType(const Type& type) {
    llvm::Type* lowered = nullptr;

    if (type.name.empty() || type.name == "int" || type.name == "i32") {
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
    (void)declaration;
    return true;
}

llvm::Type* ModuleEmitter::lowerFunctionReturnType(const FunctionDecl& declaration) {
    if (declaration.returnsVoid()) {
        return llvm::Type::getVoidTy(context_);
    }
    return lowerType(*declaration.returnType);
}

Type ModuleEmitter::inferExprType(const Expr& expr) const {
    switch (expr.kind) {
        case ExprKind::Initializer: {
            Type type;
            const auto& init = static_cast<const InitializerExpr&>(expr);
            if (init.initKind == InitKind::ArrayLiteral) {
                if (!init.values.empty()) {
                    type = inferExprType(*init.values.front());
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
        case ExprKind::StringLiteral: {
            Type type;
            type.name = "str";
            type.range = expr.range;
            return type;
        }
        case ExprKind::FloatLiteral: {
            Type type;
            type.name = "double";
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
        case ExprKind::IntegerLiteral: {
            Type type;
            type.name = "int";
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
                auto it = functionDecls_.find(*calleeName);
                if (it != functionDecls_.end() && it->second->returnType.has_value()) {
                    return *it->second->returnType;
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
            Type baseType = inferExprType(*member.base);
            auto layoutIt = aggregateLayouts_.find(baseType.name);
            if (layoutIt != aggregateLayouts_.end()) {
                auto fieldIt = layoutIt->second.fieldIndices.find(member.member);
                if (fieldIt != layoutIt->second.fieldIndices.end()) {
                    return layoutIt->second.fieldTypes[fieldIt->second];
                }
            }
            break;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            Type operandType = inferExprType(*unary.operand);
            if (unary.op == UnaryOp::AddressOf) {
                ++operandType.pointerDepth;
            } else if (unary.op == UnaryOp::Dereference && operandType.pointerDepth > 0) {
                --operandType.pointerDepth;
            }
            operandType.range = expr.range;
            return operandType;
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
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
                case BinaryOp::Assign:
                    return inferExprType(*binary.lhs);
                default:
                    return inferExprType(*binary.lhs);
            }
        }
    }

    Type fallback;
    fallback.name = "int";
    fallback.range = expr.range;
    return fallback;
}

std::optional<std::string> ModuleEmitter::inferClassTypeName(const Expr& expr) const {
    Type type = inferExprType(expr);
    if (classFieldNames_.contains(type.name) || classMethodNames_.contains(type.name)) {
        return type.name;
    }
    return std::nullopt;
}

std::optional<std::string> ModuleEmitter::moduleQualifiedName(const Expr& expr) const {
    auto name = qualifiedNameFromExpr(expr);
    if (!name.has_value()) {
        return std::nullopt;
    }
    const std::size_t split = name->find('.');
    if (split == std::string::npos) {
        return name;
    }
    const std::string root = name->substr(0, split);
    if (locals_.contains(root) || globals_.contains(root)) {
        return std::nullopt;
    }
    return name;
}

}  // namespace axc
