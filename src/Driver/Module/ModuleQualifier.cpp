#include "ModuleQualifier.h"

#include <vector>

#include "axc/Support/Diagnostic.h"
#include "axc/Support/QualifiedName.h"

namespace axc::detail {

namespace {

std::vector<std::string> splitSegments(const std::string& value) {
    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t dot = value.find('.', begin);
        if (dot == std::string::npos) {
            segments.push_back(value.substr(begin));
            break;
        }
        segments.push_back(value.substr(begin, dot - begin));
        begin = dot + 1;
    }
    return segments;
}

}  // namespace

ModuleQualifier::ModuleQualifier(DiagnosticEngine& diagnostics,
                                 const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces)
    : diagnostics_(diagnostics), moduleInterfaces_(moduleInterfaces) {}

void ModuleQualifier::qualify(TranslationUnit& unit,
                              const std::string& moduleName,
                              const ModuleImportBindings& bindings,
                              bool qualifyLocalDeclNames) const {
    ResolutionContext context;
    context.moduleName = moduleName;
    context.bindings = &bindings;
    context.qualifyLocalDeclNames = qualifyLocalDeclNames;

    for (auto& decl : unit.declarations) {
        qualifyDecl(decl, context);
    }
}

void ModuleQualifier::qualifyDecl(std::unique_ptr<Decl>& decl, const ResolutionContext& context) const {
    decl->moduleName = context.moduleName;
    const bool isMethod = decl->kind == DeclKind::Function && !static_cast<FunctionDecl&>(*decl).receiverType.empty();
    if (decl->kind != DeclKind::Import && context.qualifyLocalDeclNames && !isMethod) {
        decl->name = qualifyModuleSymbol(context.moduleName, decl->localName);
    } else if (decl->kind != DeclKind::Import) {
        decl->name = decl->localName;
    }

    switch (decl->kind) {
        case DeclKind::Import:
            break;
        case DeclKind::GlobalVar: {
            auto& global = static_cast<GlobalVarDecl&>(*decl);
            qualifyType(global.type, context);
            if (global.initializer) {
                qualifyExpr(global.initializer, context, {});
            }
            break;
        }
        case DeclKind::Struct: {
            auto& structDecl = static_cast<StructDecl&>(*decl);
            for (auto& field : structDecl.fields) {
                qualifyType(field.type, context);
                if (field.defaultValue) {
                    qualifyExpr(field.defaultValue, context, {});
                }
            }
            break;
        }
        case DeclKind::Enum: {
            auto& enumDecl = static_cast<EnumDecl&>(*decl);
            for (auto& param : enumDecl.parameters) {
                qualifyType(param.type, context);
            }
            for (auto& element : enumDecl.elements) {
                for (auto& payloadType : element.payloadTypes) {
                    qualifyType(payloadType, context);
                }
                for (auto& payloadValue : element.payloadValues) {
                    qualifyExpr(payloadValue, context, {});
                }
            }
            break;
        }
        case DeclKind::Class: {
            auto& classDecl = static_cast<ClassDecl&>(*decl);
            for (auto& includedStruct : classDecl.includedStructs) {
                includedStruct = resolveTypeName(includedStruct, context, decl->range);
            }
            for (auto& member : classDecl.members) {
                qualifyType(member.type, context);
                if (member.dynamicValue) {
                    qualifyExpr(member.dynamicValue, context, {});
                }
            }
            for (auto& method : classDecl.methods) {
                auto& functionDecl = static_cast<FunctionDecl&>(*method);
                functionDecl.receiverType = decl->name;
                qualifyDecl(method, context);
            }
            break;
        }
        case DeclKind::Function: {
            auto& functionDecl = static_cast<FunctionDecl&>(*decl);
            for (auto& param : functionDecl.compileParameters) {
                qualifyType(param.type, context);
            }
            std::unordered_set<std::string> locals;
            for (auto& param : functionDecl.runtimeParameters) {
                qualifyType(param.type, context);
                locals.insert(param.name);
            }
            for (auto& returnType : functionDecl.returnTypes) {
                qualifyType(returnType, context);
            }
            if (!functionDecl.receiverType.empty()) {
                functionDecl.receiverType = resolveTypeName(functionDecl.receiverType, context, functionDecl.range);
            }
            if (functionDecl.body) {
                std::unique_ptr<Stmt> body(functionDecl.body.release());
                qualifyStmt(body, context, std::move(locals));
                functionDecl.body.reset(static_cast<CompoundStmt*>(body.release()));
            }
            break;
        }
    }
}

void ModuleQualifier::qualifyStmt(std::unique_ptr<Stmt>& stmt,
                                  const ResolutionContext& context,
                                  std::unordered_set<std::string> locals) const {
    switch (stmt->kind) {
        case StmtKind::Compound: {
            auto& block = static_cast<CompoundStmt&>(*stmt);
            for (auto& child : block.statements) {
                qualifyStmt(child, context, locals);
            }
            break;
        }
        case StmtKind::Return: {
            auto& ret = static_cast<ReturnStmt&>(*stmt);
            for (auto& value : ret.values) {
                qualifyExpr(value, context, locals);
            }
            break;
        }
        case StmtKind::Defer: {
            auto& deferStmt = static_cast<DeferStmt&>(*stmt);
            if (deferStmt.call) {
                qualifyExpr(deferStmt.call, context, locals);
            }
            break;
        }
        case StmtKind::Expr:
            qualifyExpr(static_cast<ExprStmt&>(*stmt).expression, context, locals);
            break;
        case StmtKind::Let: {
            auto& letStmt = static_cast<LetStmt&>(*stmt);
            for (auto& binding : letStmt.bindings) {
                qualifyType(binding.explicitType, context);
            }
            if (letStmt.initializer) {
                qualifyExpr(letStmt.initializer, context, locals);
            }
            for (const auto& binding : letStmt.bindings) {
                locals.insert(binding.name);
            }
            break;
        }
        case StmtKind::If: {
            auto& ifStmt = static_cast<IfStmt&>(*stmt);
            qualifyExpr(ifStmt.condition, context, locals);
            std::unique_ptr<Stmt> thenBlock(ifStmt.thenBlock.release());
            qualifyStmt(thenBlock, context, locals);
            ifStmt.thenBlock.reset(static_cast<CompoundStmt*>(thenBlock.release()));
            if (ifStmt.elseBranch) {
                qualifyStmt(ifStmt.elseBranch, context, locals);
            }
            break;
        }
        case StmtKind::While: {
            auto& whileStmt = static_cast<WhileStmt&>(*stmt);
            qualifyExpr(whileStmt.condition, context, locals);
            std::unique_ptr<Stmt> body(whileStmt.body.release());
            qualifyStmt(body, context, locals);
            whileStmt.body.reset(static_cast<CompoundStmt*>(body.release()));
            break;
        }
        case StmtKind::For: {
            auto& forStmt = static_cast<ForStmt&>(*stmt);
            std::unordered_set<std::string> loopLocals = locals;
            if (forStmt.initializer) {
                qualifyStmt(forStmt.initializer, context, loopLocals);
                if (forStmt.initializer->kind == StmtKind::Let) {
                    const auto& letStmt = static_cast<const LetStmt&>(*forStmt.initializer);
                    for (const auto& binding : letStmt.bindings) {
                        loopLocals.insert(binding.name);
                    }
                }
            }
            if (forStmt.condition) {
                qualifyExpr(forStmt.condition, context, loopLocals);
            }
            if (forStmt.step) {
                qualifyExpr(forStmt.step, context, loopLocals);
            }
            std::unique_ptr<Stmt> body(forStmt.body.release());
            qualifyStmt(body, context, loopLocals);
            forStmt.body.reset(static_cast<CompoundStmt*>(body.release()));
            break;
        }
        case StmtKind::Foreach: {
            auto& foreachStmt = static_cast<ForeachStmt&>(*stmt);
            qualifyType(foreachStmt.bindingType, context);
            qualifyExpr(foreachStmt.iterable, context, locals);
            auto loopLocals = locals;
            loopLocals.insert(foreachStmt.bindingName);
            std::unique_ptr<Stmt> body(foreachStmt.body.release());
            qualifyStmt(body, context, loopLocals);
            foreachStmt.body.reset(static_cast<CompoundStmt*>(body.release()));
            break;
        }
        case StmtKind::DoWhile: {
            auto& doWhileStmt = static_cast<DoWhileStmt&>(*stmt);
            std::unique_ptr<Stmt> body(doWhileStmt.body.release());
            qualifyStmt(body, context, locals);
            doWhileStmt.body.reset(static_cast<CompoundStmt*>(body.release()));
            qualifyExpr(doWhileStmt.condition, context, locals);
            break;
        }
        case StmtKind::Switch: {
            auto& switchStmt = static_cast<SwitchStmt&>(*stmt);
            qualifyExpr(switchStmt.condition, context, locals);
            for (auto& switchCase : switchStmt.cases) {
                for (auto& pattern : switchCase.patterns) {
                    qualifyExpr(pattern.value, context, locals);
                }
                if (switchCase.body) {
                    std::unique_ptr<Stmt> body(switchCase.body.release());
                    qualifyStmt(body, context, locals);
                    switchCase.body.reset(static_cast<CompoundStmt*>(body.release()));
                }
            }
            break;
        }
        case StmtKind::Break:
        case StmtKind::Continue:
            break;
    }
}

void ModuleQualifier::qualifyExpr(std::unique_ptr<Expr>& expr,
                                  const ResolutionContext& context,
                                  const std::unordered_set<std::string>& locals) const {
    switch (expr->kind) {
        case ExprKind::DeclRef: {
            auto& ref = static_cast<DeclRefExpr&>(*expr);
            ref.name = resolveSimpleName(ref.name, context, locals, ref.range);
            break;
        }
        case ExprKind::Unary:
            qualifyExpr(static_cast<UnaryExpr&>(*expr).operand, context, locals);
            break;
        case ExprKind::Binary: {
            auto& binary = static_cast<BinaryExpr&>(*expr);
            qualifyExpr(binary.lhs, context, locals);
            qualifyExpr(binary.rhs, context, locals);
            break;
        }
        case ExprKind::Cast: {
            auto& cast = static_cast<CastExpr&>(*expr);
            qualifyExpr(cast.value, context, locals);
            qualifyType(cast.targetType, context);
            break;
        }
        case ExprKind::Range: {
            auto& range = static_cast<RangeExpr&>(*expr);
            qualifyExpr(range.start, context, locals);
            qualifyExpr(range.end, context, locals);
            break;
        }
        case ExprKind::Call: {
            auto& call = static_cast<CallExpr&>(*expr);
            qualifyExpr(call.callee, context, locals);
            for (auto& arg : call.compileArguments) {
                qualifyExpr(arg, context, locals);
            }
            for (auto& arg : call.runtimeArguments) {
                qualifyExpr(arg, context, locals);
            }
            break;
        }
        case ExprKind::Member: {
            auto& member = static_cast<MemberExpr&>(*expr);
            qualifyExpr(member.base, context, locals);
            if (auto qualified = qualifiedNameFromExpr(*expr); qualified.has_value()) {
                QualifiedPath path = resolveQualifiedPath(*qualified, context, expr->range);
                if (path.resolved) {
                    expr = buildQualifiedExpr(path, expr->range);
                }
            }
            break;
        }
        case ExprKind::Initializer: {
            auto& init = static_cast<InitializerExpr&>(*expr);
            if (init.typeName != "[]") {
                init.typeName = resolveTypeName(init.typeName, context, init.range);
            }
            for (auto& value : init.values) {
                qualifyExpr(value, context, locals);
            }
            break;
        }
        case ExprKind::CompileCall: {
            auto& call = static_cast<CompileCallExpr&>(*expr);
            for (auto& arg : call.arguments) {
                qualifyExpr(arg, context, locals);
            }
            break;
        }
        case ExprKind::IntegerLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::CharLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::NullLiteral:
        case ExprKind::Dialect:
            break;
    }
}

void ModuleQualifier::qualifyType(Type& type, const ResolutionContext& context) const {
    if (!type.name.empty()) {
        type.name = resolveTypeName(type.name, context, type.range);
    }
}

ModuleQualifier::QualifiedPath ModuleQualifier::resolveQualifiedPath(const std::string& name,
                                                                    const ResolutionContext& context,
                                                                    SourceRange range) const {
    QualifiedPath path;
    const std::string modulePrefix = longestKnownModulePrefix(name, context);
    if (modulePrefix.empty()) {
        return path;
    }

    std::string remainder = name.substr(modulePrefix.size());
    if (!remainder.empty() && remainder.front() == '.') {
        remainder.erase(remainder.begin());
    }
    if (remainder.empty()) {
        return path;
    }

    const std::size_t split = remainder.find('.');
    const std::string exportedName = split == std::string::npos ? remainder : remainder.substr(0, split);
    const std::string trailing = split == std::string::npos ? std::string() : remainder.substr(split + 1);

    if (modulePrefix == context.moduleName) {
        if (!context.bindings->localNames.contains(exportedName)) {
            return path;
        }
        path.baseQualifiedName = qualifyModuleSymbol(context.moduleName, exportedName);
    } else {
        auto visibleIt = context.bindings->visibleModules.find(modulePrefix);
        if (visibleIt == context.bindings->visibleModules.end()) {
            return path;
        }
        const std::string& actualModuleName = visibleIt->second;
        const auto interfaceIt = moduleInterfaces_.find(actualModuleName);
        if (interfaceIt == moduleInterfaces_.end()) {
            return path;
        }
        const ModuleInterface& interface = interfaceIt->second;
        auto exportIt = interface.exportedSymbols.find(exportedName);
        if (exportIt != interface.exportedSymbols.end()) {
            path.baseQualifiedName = exportIt->second.qualifiedName;
        } else if (interface.declaredSymbols.contains(exportedName)) {
            diagnostics_.error(range,
                               "symbol '" + exportedName + "' is private inside module '" + actualModuleName + "'");
            path.baseQualifiedName = qualifyModuleSymbol(actualModuleName, exportedName);
        } else {
            diagnostics_.error(range,
                               "module '" + actualModuleName + "' does not export '" + exportedName + "'");
            return path;
        }
    }

    if (!trailing.empty()) {
        path.trailingSegments = splitSegments(trailing);
    }
    path.resolved = true;
    return path;
}

std::string ModuleQualifier::resolveSimpleName(const std::string& name,
                                               const ResolutionContext& context,
                                               const std::unordered_set<std::string>& locals,
                                               SourceRange range) const {
    if (name.find('.') != std::string::npos) {
        QualifiedPath path = resolveQualifiedPath(name, context, range);
        return path.resolved && path.trailingSegments.empty() ? path.baseQualifiedName : name;
    }
    if (locals.contains(name)) {
        return name;
    }
    if (context.bindings->localNames.contains(name)) {
        return context.qualifyLocalDeclNames ? qualifyModuleSymbol(context.moduleName, name) : name;
    }
    if (context.bindings->ambiguousNames.contains(name)) {
        diagnostics_.error(range, "ambiguous imported symbol '" + name + "'");
        return name;
    }
    auto importedIt = context.bindings->importedSymbols.find(name);
    if (importedIt != context.bindings->importedSymbols.end()) {
        return importedIt->second.qualifiedName;
    }
    return name;
}

std::string ModuleQualifier::resolveTypeName(const std::string& name,
                                             const ResolutionContext& context,
                                             SourceRange range) const {
    if (name.empty() || isBuiltinTypeName(name)) {
        return name;
    }
    if (name.find('.') != std::string::npos) {
        QualifiedPath path = resolveQualifiedPath(name, context, range);
        if (!path.resolved) {
            return name;
        }
        std::string resolved = path.baseQualifiedName;
        for (const auto& segment : path.trailingSegments) {
            resolved += "." + segment;
        }
        return resolved;
    }
    if (context.bindings->localNames.contains(name)) {
        return context.qualifyLocalDeclNames ? qualifyModuleSymbol(context.moduleName, name) : name;
    }
    if (context.bindings->ambiguousNames.contains(name)) {
        diagnostics_.error(range, "ambiguous imported type '" + name + "'");
        return name;
    }
    auto importedIt = context.bindings->importedSymbols.find(name);
    if (importedIt != context.bindings->importedSymbols.end()) {
        return importedIt->second.qualifiedName;
    }
    return name;
}

std::unique_ptr<Expr> ModuleQualifier::buildQualifiedExpr(const QualifiedPath& path, SourceRange range) const {
    std::unique_ptr<Expr> expr = std::make_unique<DeclRefExpr>(path.baseQualifiedName, range);
    for (const auto& segment : path.trailingSegments) {
        expr = std::make_unique<MemberExpr>(std::move(expr), segment, false, range);
    }
    return expr;
}

std::string ModuleQualifier::longestKnownModulePrefix(const std::string& name, const ResolutionContext& context) const {
    std::string bestMatch;
    std::size_t split = name.find('.');
    while (split != std::string::npos) {
        const std::string candidate = name.substr(0, split);
        if (isVisibleModuleName(candidate, context)) {
            bestMatch = candidate;
        }
        split = name.find('.', split + 1);
    }
    return bestMatch;
}

bool ModuleQualifier::isVisibleModuleName(const std::string& candidate, const ResolutionContext& context) const {
    return candidate == context.moduleName || context.bindings->visibleModules.contains(candidate);
}

bool ModuleQualifier::isBuiltinTypeName(const std::string& name) const {
    static const std::unordered_set<std::string> builtinNames {
        "int", "void", "str", "error", "bool", "i2", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "short", "long", "double", "float", "f8", "f16", "f32", "f64", "char", "null"
    };
    return builtinNames.contains(name);
}

}  // namespace axc::detail
