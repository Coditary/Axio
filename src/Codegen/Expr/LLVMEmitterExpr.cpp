/// @file
/// @brief LLVM lowering for MVP expressions.

#include "../Internal/LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

namespace {

llvm::Value* toBool(llvm::IRBuilder<>& builder, llvm::LLVMContext& context, llvm::Value* value, llvm::StringRef name) {
    if (value->getType()->isIntegerTy(1)) {
        return value;
    }
    if (value->getType()->isPointerTy()) {
        return builder.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), name);
    }
    if (value->getType()->isFloatingPointTy()) {
        return builder.CreateFCmpONE(value, llvm::ConstantFP::get(value->getType(), 0.0), name);
    }
    return builder.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), name);
}

}  // namespace

llvm::Value* ModuleEmitter::emitStringConstant(const std::string& value, const std::string& nameHint) {
    auto* constant = llvm::ConstantDataArray::getString(context_, value, true);
    auto* global = new llvm::GlobalVariable(*module_, constant->getType(), true, llvm::GlobalValue::PrivateLinkage, constant, nameHint);
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    global->setAlignment(llvm::Align(1));

    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
    return builder_.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, nameHint + ".ptr");
}

llvm::Value* ModuleEmitter::emitLValue(const Expr& expr) {
    if (expr.kind == ExprKind::DeclRef) {
        const auto& ref = static_cast<const DeclRefExpr&>(expr);
        const auto symbol = lookupSymbol(ref.name);
        if (!symbol.has_value()) {
            diagnostics_.error(ref.range, "unknown variable '" + ref.name + "'");
            return nullptr;
        }
        return symbol->address;
    }

    if (expr.kind == ExprKind::Member) {
        const auto& member = static_cast<const MemberExpr&>(expr);
        const std::string aggregateName = inferExprType(*member.base).name;
        auto layoutIt = aggregateLayouts_.find(aggregateName);
        if (layoutIt == aggregateLayouts_.end()) {
            diagnostics_.error(expr.range, "member access base is not a lowered aggregate type");
            return nullptr;
        }
        auto fieldIt = layoutIt->second.fieldIndices.find(member.member);
        if (fieldIt == layoutIt->second.fieldIndices.end()) {
            diagnostics_.error(expr.range, "unknown aggregate field '" + member.member + "'");
            return nullptr;
        }
        llvm::Value* baseAddress = emitLValue(*member.base);
        if (baseAddress == nullptr) {
            return nullptr;
        }
        llvm::StructType* aggregateType = structTypes_.at(aggregateName);
        return builder_.CreateStructGEP(aggregateType, baseAddress, static_cast<unsigned>(fieldIt->second), member.member + ".addr");
    }

    if (expr.kind == ExprKind::Unary) {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        if (unary.op == UnaryOp::Dereference) {
            return emitExpr(*unary.operand);
        }
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement || unary.op == UnaryOp::PostIncrement ||
            unary.op == UnaryOp::PostDecrement) {
            return emitLValue(*unary.operand);
        }
    }

    diagnostics_.error(expr.range, "expression is not assignable");
    return nullptr;
}

llvm::Value* ModuleEmitter::emitExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral:
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), static_cast<const IntegerLiteralExpr&>(expr).value, true);
        case ExprKind::FloatLiteral:
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), static_cast<const FloatLiteralExpr&>(expr).value);
        case ExprKind::BoolLiteral:
            return llvm::ConstantInt::getBool(context_, static_cast<const BoolLiteralExpr&>(expr).value);
        case ExprKind::CharLiteral:
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), static_cast<unsigned char>(static_cast<const CharLiteralExpr&>(expr).value), false);
        case ExprKind::StringLiteral:
            return emitStringConstant(static_cast<const StringLiteralExpr&>(expr).value, "str");
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            for (const auto& [_, info] : enumValues_) {
                auto valueIt = info.values.find(ref.name);
                if (valueIt != info.values.end()) {
                    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                }
            }
            const auto symbol = lookupSymbol(ref.name);
            if (!symbol.has_value()) {
                diagnostics_.error(ref.range, "unknown variable '" + ref.name + "'");
                return nullptr;
            }
            return builder_.CreateLoad(lowerType(symbol->type), symbol->address, ref.name + ".load");
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            switch (unary.op) {
                case UnaryOp::Negate: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    return value->getType()->isFloatingPointTy() ? builder_.CreateFNeg(value, "fneg") : builder_.CreateNeg(value, "neg");
                }
                case UnaryOp::AddressOf:
                    return emitLValue(*unary.operand);
                case UnaryOp::Dereference: {
                    llvm::Value* pointer = emitExpr(*unary.operand);
                    if (pointer == nullptr || !pointer->getType()->isPointerTy()) {
                        diagnostics_.error(unary.range, "cannot dereference a non-pointer value");
                        return nullptr;
                    }
                    Type pointeeType = inferExprType(expr);
                    return builder_.CreateLoad(lowerType(pointeeType), pointer, "deref");
                }
                case UnaryOp::LogicalNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    return builder_.CreateNot(toBool(builder_, context_, value, "not.bool"), "not");
                }
                case UnaryOp::BitwiseNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    return value ? builder_.CreateNot(value, "bnot") : nullptr;
                }
                case UnaryOp::PreIncrement:
                case UnaryOp::PreDecrement:
                case UnaryOp::PostIncrement:
                case UnaryOp::PostDecrement: {
                    llvm::Value* address = emitLValue(*unary.operand);
                    llvm::Value* current = emitExpr(*unary.operand);
                    if (address == nullptr || current == nullptr) {
                        return nullptr;
                    }
                    llvm::Value* delta = current->getType()->isFloatingPointTy()
                        ? static_cast<llvm::Value*>(llvm::ConstantFP::get(current->getType(), 1.0))
                        : static_cast<llvm::Value*>(llvm::ConstantInt::get(current->getType(), 1));
                    llvm::Value* updated = nullptr;
                    if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PostIncrement) {
                        updated = current->getType()->isFloatingPointTy() ? builder_.CreateFAdd(current, delta, "inc") : builder_.CreateAdd(current, delta, "inc");
                    } else {
                        updated = current->getType()->isFloatingPointTy() ? builder_.CreateFSub(current, delta, "dec") : builder_.CreateSub(current, delta, "dec");
                    }
                    builder_.CreateStore(updated, address);
                    return (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement) ? updated : current;
                }
            }
            return nullptr;
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            if (binary.op == BinaryOp::Assign) {
                llvm::Value* address = emitLValue(*binary.lhs);
                llvm::Value* value = emitExpr(*binary.rhs);
                if (address == nullptr || value == nullptr) {
                    return nullptr;
                }
                Type lhsType = inferExprType(*binary.lhs);
                value = castValueToType(value, lhsType);
                builder_.CreateStore(value, address);
                return value;
            }

            llvm::Value* lhs = emitExpr(*binary.lhs);
            llvm::Value* rhs = emitExpr(*binary.rhs);
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }

            const bool isFloat = lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy();
            switch (binary.op) {
                case BinaryOp::Add:
                    return isFloat ? builder_.CreateFAdd(lhs, rhs, "fadd") : builder_.CreateAdd(lhs, rhs, "add");
                case BinaryOp::Sub:
                    return isFloat ? builder_.CreateFSub(lhs, rhs, "fsub") : builder_.CreateSub(lhs, rhs, "sub");
                case BinaryOp::Mul:
                    return isFloat ? builder_.CreateFMul(lhs, rhs, "fmul") : builder_.CreateMul(lhs, rhs, "mul");
                case BinaryOp::Div:
                    return isFloat ? builder_.CreateFDiv(lhs, rhs, "fdiv") : builder_.CreateSDiv(lhs, rhs, "div");
                case BinaryOp::Mod:
                    return builder_.CreateSRem(lhs, rhs, "mod");
                case BinaryOp::BitAnd:
                    return builder_.CreateAnd(lhs, rhs, "band");
                case BinaryOp::BitOr:
                    return builder_.CreateOr(lhs, rhs, "bor");
                case BinaryOp::BitXor:
                    return builder_.CreateXor(lhs, rhs, "bxor");
                case BinaryOp::ShiftLeft:
                    return builder_.CreateShl(lhs, rhs, "shl");
                case BinaryOp::ShiftRight:
                    return builder_.CreateAShr(lhs, rhs, "shr");
                case BinaryOp::Equal:
                    return isFloat ? builder_.CreateFCmpOEQ(lhs, rhs, "feq") : builder_.CreateICmpEQ(lhs, rhs, "eq");
                case BinaryOp::NotEqual:
                    return isFloat ? builder_.CreateFCmpONE(lhs, rhs, "fne") : builder_.CreateICmpNE(lhs, rhs, "ne");
                case BinaryOp::Less:
                    return isFloat ? builder_.CreateFCmpOLT(lhs, rhs, "flt") : builder_.CreateICmpSLT(lhs, rhs, "lt");
                case BinaryOp::LessEqual:
                    return isFloat ? builder_.CreateFCmpOLE(lhs, rhs, "fle") : builder_.CreateICmpSLE(lhs, rhs, "le");
                case BinaryOp::Greater:
                    return isFloat ? builder_.CreateFCmpOGT(lhs, rhs, "fgt") : builder_.CreateICmpSGT(lhs, rhs, "gt");
                case BinaryOp::GreaterEqual:
                    return isFloat ? builder_.CreateFCmpOGE(lhs, rhs, "fge") : builder_.CreateICmpSGE(lhs, rhs, "ge");
                case BinaryOp::LogicalAnd:
                    return builder_.CreateAnd(toBool(builder_, context_, lhs, "land.lhs"), toBool(builder_, context_, rhs, "land.rhs"), "land");
                case BinaryOp::LogicalOr:
                    return builder_.CreateOr(toBool(builder_, context_, lhs, "lor.lhs"), toBool(builder_, context_, rhs, "lor.rhs"), "lor");
                case BinaryOp::Assign:
                    break;
            }
            return nullptr;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                if (callee.name == "len") {
                    if (call.arguments.size() != 1) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to 'len'");
                        return nullptr;
                    }
                    Type arrayType = inferExprType(*call.arguments[0]);
                    if (arrayType.arrayExtents.empty() || !arrayType.arrayExtents.front().has_value()) {
                        diagnostics_.error(call.range, "len expects an array argument");
                        return nullptr;
                    }
                    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), *arrayType.arrayExtents.front(), false);
                }
                auto enumIt = enumValues_.find(callee.name);
                if (enumIt != enumValues_.end() && call.arguments.size() == 1) {
                    return emitExpr(*call.arguments[0]);
                }
            }

            std::optional<std::string> loweredName;
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (auto className = inferClassTypeName(*member.base); className.has_value()) {
                    loweredName = *className + "." + member.member;
                }
            }
            if (!loweredName.has_value()) {
                loweredName = moduleQualifiedName(*call.callee);
            }
            if (!loweredName.has_value()) {
                diagnostics_.error(call.range, "call target must resolve to a declared function or method");
                return nullptr;
            }

            auto fnIt = functions_.find(*loweredName);
            if (fnIt == functions_.end()) {
                diagnostics_.error(call.range, "unknown function '" + *loweredName + "'");
                return nullptr;
            }
            const FunctionDecl* functionDecl = functionDecls_[*loweredName];

            std::vector<llvm::Value*> args;
            args.reserve(call.arguments.size() + (call.callee->kind == ExprKind::Member ? 1U : 0U));
            std::size_t paramOffset = 0;
            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                llvm::Value* selfValue = emitExpr(*member.base);
                if (selfValue == nullptr) {
                    return nullptr;
                }
                args.push_back(castValueToType(selfValue, functionDecl->parameters.front().type));
                paramOffset = 1;
            }

            if (functionDecl->parameters.size() != call.arguments.size() + paramOffset) {
                diagnostics_.error(call.range, "wrong number of arguments for call to '" + *loweredName + "'");
                return nullptr;
            }

            for (std::size_t i = 0; i < call.arguments.size(); ++i) {
                llvm::Value* value = emitExpr(*call.arguments[i]);
                if (value == nullptr) {
                    return nullptr;
                }
                args.push_back(castValueToType(value, functionDecl->parameters[i + paramOffset].type));
            }

            llvm::Function* function = fnIt->second;
            return builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
        }
        case ExprKind::Member: {
            if (auto qualified = qualifiedNameFromExpr(expr); qualified.has_value()) {
                for (const auto& [enumName, info] : enumValues_) {
                    const std::string prefix = enumName + ".";
                    if (qualified->rfind(prefix, 0) == 0) {
                        auto valueIt = info.values.find(qualified->substr(prefix.size()));
                        if (valueIt != info.values.end()) {
                            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                        }
                    }
                }
            }
            llvm::Value* address = emitLValue(expr);
            if (address == nullptr) {
                return nullptr;
            }
            Type memberType = inferExprType(expr);
            return builder_.CreateLoad(lowerType(memberType), address, "member.load");
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            Type initType = inferExprType(expr);
            llvm::Type* llvmType = lowerType(initType);
            if (init.initKind == InitKind::ArrayLiteral) {
                llvm::Value* aggregate = llvm::UndefValue::get(llvmType);
                for (std::size_t i = 0; i < init.values.size(); ++i) {
                    llvm::Value* value = emitExpr(*init.values[i]);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    aggregate = builder_.CreateInsertValue(aggregate, castValueToType(value, initType), {static_cast<unsigned>(i)}, "array.insert");
                }
                return aggregate;
            }
            auto layoutIt = aggregateLayouts_.find(init.typeName);
            if (layoutIt == aggregateLayouts_.end()) {
                diagnostics_.error(expr.range, "unknown initializer type '" + init.typeName + "'");
                return nullptr;
            }
            llvm::Value* aggregate = llvm::UndefValue::get(llvmType);
            for (std::size_t i = 0; i < init.values.size() && i < layoutIt->second.fieldTypes.size(); ++i) {
                llvm::Value* value = emitExpr(*init.values[i]);
                if (value == nullptr) {
                    return nullptr;
                }
                aggregate = builder_.CreateInsertValue(aggregate, castValueToType(value, layoutIt->second.fieldTypes[i]), {static_cast<unsigned>(i)}, "init.insert");
            }
            return aggregate;
        }
    }

    return nullptr;
}

}  // namespace axc
