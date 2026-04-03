#include "LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

llvm::Value* ModuleEmitter::emitStringConstant(const std::string& value, const std::string& nameHint) {
    auto* constant = llvm::ConstantDataArray::getString(context_, value, true);
    auto* global = new llvm::GlobalVariable(*module_, constant->getType(), true, llvm::GlobalValue::PrivateLinkage, constant, nameHint);
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    global->setAlignment(llvm::Align(1));

    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
    return builder_.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, nameHint + ".ptr");
}

MultiValue ModuleEmitter::emitExprValues(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (!call.compileArguments.empty()) {
                diagnostics_.error(call.range, "codegen for compile-time call arguments is not implemented yet");
                return {};
            }

            if (auto qualifiedCallee = moduleQualifiedName(*call.callee); qualifiedCallee.has_value()) {
                auto enumIt = enumValues_.find(*qualifiedCallee);
                if (enumIt != enumValues_.end() && call.runtimeArguments.size() == 1) {
                    llvm::Value* value = emitExpr(*call.runtimeArguments[0]);
                    return value == nullptr ? MultiValue {} : MultiValue {{value}};
                }

                auto it = functions_.find(*qualifiedCallee);
                if (it != functions_.end()) {
                    llvm::Function* function = it->second;
                    if (function->arg_size() != call.runtimeArguments.size()) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to '" + *qualifiedCallee + "'");
                        return {};
                    }

                    std::vector<llvm::Value*> args;
                    args.reserve(call.runtimeArguments.size());
                    const FunctionDecl* functionDecl = functionDecls_[*qualifiedCallee];
                    for (const auto& argument : call.runtimeArguments) {
                        llvm::Value* value = emitExpr(*argument);
                        if (value == nullptr) {
                            return {};
                        }
                        args.push_back(castValueToType(value, functionDecl->runtimeParameters[args.size()].type));
                    }

                    llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                    if (functionDecl->returnTypes.size() <= 1) {
                        return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                    }

                    MultiValue result;
                    result.values.reserve(functionDecl->returnTypes.size());
                    for (std::size_t i = 0; i < functionDecl->returnTypes.size(); ++i) {
                        result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                    }
                    return result;
                }
            }

            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (member.nullSafe && member.base->kind == ExprKind::DeclRef) {
                    const auto& base = static_cast<const DeclRefExpr&>(*member.base);
                    const auto symbol = lookupSymbol(base.name);
                    if (!symbol.has_value()) {
                        diagnostics_.error(call.range, "unknown variable '" + base.name + "'");
                        return {};
                    }
                    if (!symbol->nullable) {
                        diagnostics_.warning(call.range, "null-safe call lowers as a direct call because the value is not known nullable");
                    }
                    return MultiValue {{llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false)}};
                }
                if (auto className = inferClassTypeName(*member.base); className.has_value()) {
                    const std::string loweredName = *className + "." + member.member;
                    auto it = functions_.find(loweredName);
                    if (it != functions_.end()) {
                        llvm::Function* function = it->second;
                        const FunctionDecl* functionDecl = functionDecls_[loweredName];
                        std::vector<llvm::Value*> args;
                        args.reserve(call.runtimeArguments.size() + 1);
                        llvm::Value* selfValue = emitExpr(*member.base);
                        if (selfValue == nullptr) {
                            return {};
                        }
                        args.push_back(castValueToType(selfValue, functionDecl->runtimeParameters[0].type));
                        for (const auto& argument : call.runtimeArguments) {
                            llvm::Value* value = emitExpr(*argument);
                            if (value == nullptr) {
                                return {};
                            }
                            args.push_back(castValueToType(value, functionDecl->runtimeParameters[args.size()].type));
                        }
                        llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                        if (functionDecl->returnTypes.size() <= 1) {
                            return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                        }
                        MultiValue result;
                        result.values.reserve(functionDecl->returnTypes.size());
                        for (std::size_t i = 0; i < functionDecl->returnTypes.size(); ++i) {
                            result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                        }
                        return result;
                    }
                }
            }

            if (call.callee->kind == ExprKind::DeclRef) {
                const auto* ref = static_cast<const DeclRefExpr*>(call.callee.get());
                auto enumIt = enumValues_.find(ref->name);
                if (enumIt != enumValues_.end() && call.runtimeArguments.size() == 1) {
                    llvm::Value* value = emitExpr(*call.runtimeArguments[0]);
                    return value == nullptr ? MultiValue {} : MultiValue {{value}};
                }
            }

            if (call.callee->kind == ExprKind::DeclRef) {
                const auto* ref = static_cast<const DeclRefExpr*>(call.callee.get());
                auto it = functions_.find(ref->name);
                if (it == functions_.end()) {
                    diagnostics_.error(call.range, "unknown function '" + ref->name + "'");
                    return {};
                }

                llvm::Function* function = it->second;
                if (function->arg_size() != call.runtimeArguments.size()) {
                    diagnostics_.error(call.range, "wrong number of arguments for call to '" + ref->name + "'");
                    return {};
                }

                std::vector<llvm::Value*> args;
                args.reserve(call.runtimeArguments.size());
                const FunctionDecl* functionDecl = functionDecls_[ref->name];
                for (const auto& argument : call.runtimeArguments) {
                    llvm::Value* value = emitExpr(*argument);
                    if (value == nullptr) {
                        return {};
                    }
                    args.push_back(castValueToType(value, functionDecl->runtimeParameters[args.size()].type));
                }

                llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                if (functionDecl->returnTypes.size() <= 1) {
                    return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                }

                MultiValue result;
                result.values.reserve(functionDecl->returnTypes.size());
                for (std::size_t i = 0; i < functionDecl->returnTypes.size(); ++i) {
                    result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                }
                return result;
            }

            diagnostics_.error(call.range, "only direct function calls are lowered in this prototype");
            return {};
        }
        default: {
            llvm::Value* value = emitExpr(expr);
            return value == nullptr ? MultiValue {} : MultiValue {{value}};
        }
    }
}

llvm::Value* ModuleEmitter::emitCompileCall(const CompileCallExpr& call) {
    if ((call.callee != "readfile" && call.callee != "generate_open_api") || call.arguments.empty() ||
        call.arguments.front()->kind != ExprKind::StringLiteral) {
        diagnostics_.error(call.range, "unsupported compile function expression");
        return nullptr;
    }

    const auto* pathExpr = static_cast<const StringLiteralExpr*>(call.arguments.front().get());
    const std::filesystem::path baseDir = sourceManager_.path().parent_path();
    const std::filesystem::path filePath = baseDir / pathExpr->value;

    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        diagnostics_.error(call.range, "failed to open compile-time file '" + filePath.string() + "'");
        return nullptr;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return emitStringConstant(buffer.str(), call.callee == "readfile" ? "readfile" : "openapi");
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
        Type baseType = inferExprType(*member.base);
        auto layoutIt = aggregateLayouts_.find(baseType.name);
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
        llvm::StructType* aggregateType = structTypes_.at(baseType.name);
        return builder_.CreateStructGEP(aggregateType, baseAddress, static_cast<unsigned>(fieldIt->second), member.member + ".addr");
    }

    if (expr.kind == ExprKind::Unary) {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        if (unary.op == UnaryOp::Dereference) {
            return emitExpr(*unary.operand);
        }
    }

    diagnostics_.error(expr.range, "expression is not assignable");
    return nullptr;
}

llvm::Value* ModuleEmitter::emitExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral: {
            const auto& literal = static_cast<const IntegerLiteralExpr&>(expr);
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), literal.value, true);
        }
        case ExprKind::FloatLiteral: {
            const auto& literal = static_cast<const FloatLiteralExpr&>(expr);
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), literal.value);
        }
        case ExprKind::BoolLiteral: {
            const auto& literal = static_cast<const BoolLiteralExpr&>(expr);
            return llvm::ConstantInt::getBool(context_, literal.value);
        }
        case ExprKind::CharLiteral: {
            const auto& literal = static_cast<const CharLiteralExpr&>(expr);
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), static_cast<unsigned char>(literal.value), false);
        }
        case ExprKind::StringLiteral: {
            const auto& literal = static_cast<const StringLiteralExpr&>(expr);
            return emitStringConstant(literal.value, "str");
        }
        case ExprKind::NullLiteral:
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(context_, 0));
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto enumIt = enumValues_.find(ref.name);
            if (enumIt != enumValues_.end()) {
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
            }
            for (const auto& [enumName, info] : enumValues_) {
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
                    return value ? builder_.CreateNeg(value, "negtmp") : nullptr;
                }
                case UnaryOp::AddressOf:
                    return emitLValue(*unary.operand);
                case UnaryOp::Dereference: {
                    llvm::Value* pointer = emitExpr(*unary.operand);
                    if (pointer == nullptr || !pointer->getType()->isPointerTy()) {
                        diagnostics_.error(unary.range, "cannot dereference a non-pointer value");
                        return nullptr;
                    }
                    return builder_.CreateLoad(llvm::Type::getInt32Ty(context_), pointer, "deref");
                }
                case UnaryOp::LogicalNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    return value ? builder_.CreateNot(value, "nottmp") : nullptr;
                }
                case UnaryOp::BitwiseNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    return value ? builder_.CreateNot(value, "bnot") : nullptr;
                }
                case UnaryOp::IsNonNull: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    if (value == nullptr || !value->getType()->isPointerTy()) {
                        diagnostics_.error(unary.range, "postfix '?' currently requires a pointer value");
                        return nullptr;
                    }
                    return builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "nonnull");
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
                builder_.CreateStore(value, address);
                return value;
            }

            if (binary.op == BinaryOp::InRange) {
                const auto* range = dynamic_cast<const RangeExpr*>(binary.rhs.get());
                if (range == nullptr) {
                    diagnostics_.error(binary.range, "range membership expects a range expression on the right-hand side");
                    return nullptr;
                }
                llvm::Value* value = emitExpr(*binary.lhs);
                llvm::Value* start = emitExpr(*range->start);
                llvm::Value* end = emitExpr(*range->end);
                if (value == nullptr || start == nullptr || end == nullptr) {
                    return nullptr;
                }
                llvm::Value* lower = builder_.CreateICmpSGE(value, start, "range.lower");
                llvm::Value* upper = range->inclusive ? builder_.CreateICmpSLE(value, end, "range.upper") : builder_.CreateICmpSLT(value, end, "range.upper");
                return builder_.CreateAnd(lower, upper, "inrange");
            }

            llvm::Value* lhs = emitExpr(*binary.lhs);
            llvm::Value* rhs = emitExpr(*binary.rhs);
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }

            const bool isFloat = lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy();

            switch (binary.op) {
                case BinaryOp::Add:
                    return isFloat ? builder_.CreateFAdd(lhs, rhs, "faddtmp") : builder_.CreateAdd(lhs, rhs, "addtmp");
                case BinaryOp::Sub:
                    return isFloat ? builder_.CreateFSub(lhs, rhs, "fsubtmp") : builder_.CreateSub(lhs, rhs, "subtmp");
                case BinaryOp::Mul:
                    return isFloat ? builder_.CreateFMul(lhs, rhs, "fmultmp") : builder_.CreateMul(lhs, rhs, "multmp");
                case BinaryOp::Div:
                    return isFloat ? builder_.CreateFDiv(lhs, rhs, "fdivtmp") : builder_.CreateSDiv(lhs, rhs, "divtmp");
                case BinaryOp::Mod:
                    return builder_.CreateSRem(lhs, rhs, "modtmp");
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
                    return builder_.CreateAnd(lhs, rhs, "land");
                case BinaryOp::LogicalOr:
                    return builder_.CreateOr(lhs, rhs, "lor");
                case BinaryOp::Set:
                    return builder_.CreateOr(lhs, rhs, "enum.set");
                case BinaryOp::Unset: {
                    llvm::Value* inverted = builder_.CreateNot(rhs, "enum.unset.mask");
                    return builder_.CreateAnd(lhs, inverted, "enum.unset");
                }
                case BinaryOp::Toggle:
                    return builder_.CreateXor(lhs, rhs, "enum.toggle");
                case BinaryOp::Is: {
                    llvm::Value* masked = builder_.CreateAnd(lhs, rhs, "enum.is.masked");
                    return builder_.CreateICmpEQ(masked, rhs, "enum.is");
                }
                case BinaryOp::IsNot: {
                    llvm::Value* masked = builder_.CreateAnd(lhs, rhs, "enum.isnot.masked");
                    return builder_.CreateICmpNE(masked, rhs, "enum.isnot");
                }
                case BinaryOp::Assign:
                case BinaryOp::InRange:
                    break;
            }
            return nullptr;
        }
        case ExprKind::Range:
            diagnostics_.error(expr.range, "standalone range expressions are not first-class runtime values yet");
            return nullptr;
        case ExprKind::Call: {
            MultiValue values = emitExprValues(expr);
            if (values.values.empty()) {
                return nullptr;
            }
            if (values.values.size() != 1) {
                diagnostics_.error(expr.range, "multi-value expressions require destructuring or multi-return forwarding");
                return nullptr;
            }
            return values.values.front();
        }
        case ExprKind::Member:
            if (const auto* member = dynamic_cast<const MemberExpr*>(&expr)) {
                if (auto qualified = moduleQualifiedName(expr); qualified.has_value()) {
                    for (const auto& [enumName, info] : enumValues_) {
                        const std::string prefix = enumName + ".";
                        if (qualified->rfind(prefix, 0) == 0) {
                            const std::string suffix = qualified->substr(prefix.size());
                            auto valueIt = info.values.find(suffix);
                            if (valueIt != info.values.end()) {
                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                            }
                            for (const auto& [variant, params] : info.paramValues) {
                                if (variant == suffix) {
                                    continue;
                                }
                            }
                        }
                    }
                }
                Type baseType = inferExprType(*member->base);
                auto aggregateLayout = aggregateLayouts_.find(baseType.name);
                if (aggregateLayout != aggregateLayouts_.end()) {
                    auto fieldIt = aggregateLayout->second.fieldIndices.find(member->member);
                    if (fieldIt != aggregateLayout->second.fieldIndices.end()) {
                        llvm::Value* fieldAddress = emitLValue(expr);
                        if (fieldAddress == nullptr) {
                            return nullptr;
                        }
                        return builder_.CreateLoad(lowerType(aggregateLayout->second.fieldTypes[fieldIt->second]), fieldAddress, member->member + ".load");
                    }
                }
                if (auto className = inferClassTypeName(*member->base); className.has_value()) {
                    auto layoutIt = aggregateLayouts_.find(*className);
                    if (layoutIt != aggregateLayouts_.end()) {
                        auto fieldIt = layoutIt->second.fieldIndices.find(member->member);
                        if (fieldIt != layoutIt->second.fieldIndices.end()) {
                            llvm::Value* fieldAddress = emitLValue(expr);
                            if (fieldAddress == nullptr) {
                                return nullptr;
                            }
                            return builder_.CreateLoad(lowerType(layoutIt->second.fieldTypes[fieldIt->second]), fieldAddress, member->member + ".load");
                        }
                    }
                }
                if (auto className = inferClassTypeName(*member->base); className.has_value()) {
                    if (classFieldNames_.contains(*className) && classFieldNames_.at(*className).contains(member->member)) {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                    if (classMethodNames_.contains(*className) && classMethodNames_.at(*className).contains(member->member)) {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                }
                if (member->base->kind == ExprKind::DeclRef) {
                    const auto& base = static_cast<const DeclRefExpr&>(*member->base);
                    const auto symbol = lookupSymbol(base.name);
                    if (symbol.has_value() && symbol->type.name == "Obj") {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                }
                if (member->base->kind == ExprKind::DeclRef) {
                    const auto& base = static_cast<const DeclRefExpr&>(*member->base);
                    auto enumIt = enumValues_.find(base.name);
                    if (enumIt != enumValues_.end()) {
                        auto valueIt = enumIt->second.values.find(member->member);
                        if (valueIt != enumIt->second.values.end()) {
                            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                        }
                        for (const auto& [variant, params] : enumIt->second.paramValues) {
                            auto paramIt = params.find(member->member);
                            if (paramIt != params.end()) {
                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                            }
                        }
                    }
                }
                if (member->base->kind == ExprKind::Call) {
                    const auto& call = static_cast<const CallExpr&>(*member->base);
                    if (call.callee->kind == ExprKind::DeclRef && !call.runtimeArguments.empty()) {
                        const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                        auto enumIt = enumValues_.find(callee.name);
                        if (enumIt != enumValues_.end()) {
                            llvm::Value* ordinalValue = emitExpr(*call.runtimeArguments[0]);
                            if (auto* ordinalConst = llvm::dyn_cast_or_null<llvm::ConstantInt>(ordinalValue)) {
                                const std::uint64_t ordinal = ordinalConst->getZExtValue();
                                for (const auto& [variant, value] : enumIt->second.values) {
                                    if (value == ordinal) {
                                        auto paramsIt = enumIt->second.paramValues.find(variant);
                                        if (paramsIt != enumIt->second.paramValues.end()) {
                                            auto paramIt = paramsIt->second.find(member->member);
                                            if (paramIt != paramsIt->second.end()) {
                                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            diagnostics_.error(expr.range, "member access codegen is not implemented yet");
            return nullptr;
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            auto enumIt = enumValues_.find(init.typeName);
            if (enumIt != enumValues_.end() && enumIt->second.isFlags) {
                std::uint64_t value = 0;
                for (const auto& item : init.values) {
                    llvm::Value* lowered = emitExpr(*item);
                    if (auto* constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(lowered)) {
                        value |= constant->getZExtValue();
                    } else {
                        diagnostics_.error(item->range, "flag enum initializer requires constant enum members");
                        return nullptr;
                    }
                }
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), value, false);
            }
            auto it = structTypes_.find(init.typeName);
            if (it == structTypes_.end()) {
                diagnostics_.error(expr.range, "initializer targets unknown type '" + init.typeName + "'");
                return nullptr;
            }
            auto layoutIt = aggregateLayouts_.find(init.typeName);
            if (layoutIt == aggregateLayouts_.end()) {
                diagnostics_.error(expr.range, "initializer layout is unknown for type '" + init.typeName + "'");
                return nullptr;
            }
            llvm::Value* aggregate = llvm::UndefValue::get(it->second);
            const std::size_t count = std::min(init.values.size(), layoutIt->second.fieldTypes.size());
            for (std::size_t i = 0; i < count; ++i) {
                llvm::Value* value = emitExpr(*init.values[i]);
                if (value == nullptr) {
                    return nullptr;
                }
                aggregate = builder_.CreateInsertValue(aggregate, castValueToType(value, layoutIt->second.fieldTypes[i]), {static_cast<unsigned>(i)}, init.typeName + ".init");
            }
            return aggregate;
        }
        case ExprKind::CompileCall:
            return emitCompileCall(static_cast<const CompileCallExpr&>(expr));
        case ExprKind::Dialect:
            diagnostics_.error(expr.range, "dialect blocks are not lowered to runtime values yet");
            return nullptr;
    }

    return nullptr;
}

}  // namespace axc
