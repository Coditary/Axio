#include "ModuleLoader.h"

#include <utility>

#include "axc/Lex/Lexer.h"
#include "axc/Parse/Parser.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

ModuleLoader::ModuleLoader(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool ModuleLoader::loadInto(TranslationUnit& rootUnit, const std::filesystem::path& entryFile) {
    const std::filesystem::path projectRoot = entryFile.parent_path();
    std::vector<std::unique_ptr<Decl>> mergedDecls;
    std::vector<std::unique_ptr<Decl>> rootDecls;
    rootDecls.swap(rootUnit.declarations);

    for (auto& decl : rootDecls) {
        if (decl->kind == DeclKind::Import) {
            if (!loadModuleRecursive(rootUnit, projectRoot, entryFile, static_cast<const ImportDecl&>(*decl))) {
                return false;
            }
            continue;
        }
        mergedDecls.push_back(std::move(decl));
    }

    for (auto& decl : mergedDecls) {
        rootUnit.declarations.push_back(std::move(decl));
    }
    return !diagnostics_.hasErrors();
}

bool ModuleLoader::loadModuleRecursive(TranslationUnit& destination,
                                       const std::filesystem::path& projectRoot,
                                       const std::filesystem::path& importerFile,
                                       const ImportDecl& importDecl) {
    const std::string moduleName = joinSegments(importDecl.moduleSegments);
    if (loadedModules_.contains(moduleName)) {
        return true;
    }

    const std::filesystem::path moduleFile = modulePathFor(projectRoot, importDecl);
    moduleFiles_[moduleName] = moduleFile;
    loadedModules_.insert(moduleName);

    TranslationUnit importedUnit;
    if (!parseFile(moduleFile, importedUnit)) {
        diagnostics_.error(importDecl.range, "failed to load module '" + moduleName + "' from '" + moduleFile.string() + "'");
        return false;
    }

    const auto topLevelNames = collectTopLevelNames(importedUnit);
    std::vector<std::unique_ptr<Decl>> importedDecls;
    importedDecls.swap(importedUnit.declarations);
    for (auto& decl : importedDecls) {
        if (decl->kind == DeclKind::Import) {
            if (!loadModuleRecursive(destination, projectRoot, moduleFile, static_cast<const ImportDecl&>(*decl))) {
                return false;
            }
            continue;
        }
        qualifyDecl(*decl, moduleName, topLevelNames);
        destination.declarations.push_back(std::move(decl));
    }

    (void)importerFile;
    return true;
}

bool ModuleLoader::parseFile(const std::filesystem::path& path, TranslationUnit& unit) {
    SourceManager sourceManager;
    std::string errorMessage;
    if (!sourceManager.loadFromFile(path, errorMessage)) {
        return false;
    }

    DiagnosticEngine localDiagnostics(sourceManager);
    Lexer lexer(sourceManager, localDiagnostics);
    std::vector<Token> tokens = lexer.lex();
    Parser parser(std::move(tokens), localDiagnostics);
    unit = parser.parseTranslationUnit();

    if (localDiagnostics.hasErrors()) {
        for (const auto& diagnostic : localDiagnostics.diagnostics()) {
            diagnostics_.error(diagnostic.range, diagnostic.message);
        }
        return false;
    }
    return true;
}

void ModuleLoader::qualifyModuleDecls(TranslationUnit& unit, const std::string& moduleName) const {
    const auto topLevelNames = collectTopLevelNames(unit);
    for (auto& decl : unit.declarations) {
        if (decl->kind != DeclKind::Import) {
            qualifyDecl(*decl, moduleName, topLevelNames);
        }
    }
}

void ModuleLoader::qualifyType(Type& type, const std::string& moduleName, const std::unordered_set<std::string>& topLevelNames) const {
    if (!type.name.empty() && !isQualified(type.name) && !isBuiltinTypeName(type.name) && topLevelNames.contains(type.name)) {
        type.name = moduleName + "." + type.name;
    }
}

void ModuleLoader::qualifyExpr(Expr& expr,
                               const std::string& moduleName,
                               const std::unordered_set<std::string>& topLevelNames,
                               const std::unordered_set<std::string>& locals) const {
    switch (expr.kind) {
        case ExprKind::DeclRef: {
            auto& ref = static_cast<DeclRefExpr&>(expr);
            if (!isQualified(ref.name) && topLevelNames.contains(ref.name) && !locals.contains(ref.name)) {
                ref.name = moduleName + "." + ref.name;
            }
            break;
        }
        case ExprKind::Unary:
            qualifyExpr(*static_cast<UnaryExpr&>(expr).operand, moduleName, topLevelNames, locals);
            break;
        case ExprKind::Binary: {
            auto& binary = static_cast<BinaryExpr&>(expr);
            qualifyExpr(*binary.lhs, moduleName, topLevelNames, locals);
            qualifyExpr(*binary.rhs, moduleName, topLevelNames, locals);
            break;
        }
        case ExprKind::Range: {
            auto& range = static_cast<RangeExpr&>(expr);
            qualifyExpr(*range.start, moduleName, topLevelNames, locals);
            qualifyExpr(*range.end, moduleName, topLevelNames, locals);
            break;
        }
        case ExprKind::Call: {
            auto& call = static_cast<CallExpr&>(expr);
            qualifyExpr(*call.callee, moduleName, topLevelNames, locals);
            for (auto& arg : call.compileArguments) {
                qualifyExpr(*arg, moduleName, topLevelNames, locals);
            }
            for (auto& arg : call.runtimeArguments) {
                qualifyExpr(*arg, moduleName, topLevelNames, locals);
            }
            break;
        }
        case ExprKind::Member:
            qualifyExpr(*static_cast<MemberExpr&>(expr).base, moduleName, topLevelNames, locals);
            break;
        case ExprKind::Initializer: {
            auto& init = static_cast<InitializerExpr&>(expr);
            if (!isQualified(init.typeName) && init.typeName != "[]" && topLevelNames.contains(init.typeName)) {
                init.typeName = moduleName + "." + init.typeName;
            }
            for (auto& value : init.values) {
                qualifyExpr(*value, moduleName, topLevelNames, locals);
            }
            break;
        }
        case ExprKind::CompileCall: {
            for (auto& arg : static_cast<CompileCallExpr&>(expr).arguments) {
                qualifyExpr(*arg, moduleName, topLevelNames, locals);
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

void ModuleLoader::qualifyStmt(Stmt& stmt,
                               const std::string& moduleName,
                               const std::unordered_set<std::string>& topLevelNames,
                               std::unordered_set<std::string> locals) const {
    switch (stmt.kind) {
        case StmtKind::Compound:
            for (auto& child : static_cast<CompoundStmt&>(stmt).statements) {
                qualifyStmt(*child, moduleName, topLevelNames, locals);
            }
            break;
        case StmtKind::Return:
            for (auto& value : static_cast<ReturnStmt&>(stmt).values) {
                qualifyExpr(*value, moduleName, topLevelNames, locals);
            }
            break;
        case StmtKind::Expr:
            qualifyExpr(*static_cast<ExprStmt&>(stmt).expression, moduleName, topLevelNames, locals);
            break;
        case StmtKind::Let: {
            auto& letStmt = static_cast<LetStmt&>(stmt);
            for (auto& binding : letStmt.bindings) {
                qualifyType(binding.explicitType, moduleName, topLevelNames);
            }
            if (letStmt.initializer) {
                qualifyExpr(*letStmt.initializer, moduleName, topLevelNames, locals);
            }
            for (const auto& binding : letStmt.bindings) {
                locals.insert(binding.name);
            }
            break;
        }
        case StmtKind::If: {
            auto& ifStmt = static_cast<IfStmt&>(stmt);
            qualifyExpr(*ifStmt.condition, moduleName, topLevelNames, locals);
            qualifyStmt(*ifStmt.thenBlock, moduleName, topLevelNames, locals);
            if (ifStmt.elseBranch) {
                qualifyStmt(*ifStmt.elseBranch, moduleName, topLevelNames, locals);
            }
            break;
        }
    }
}

void ModuleLoader::qualifyDecl(Decl& decl, const std::string& moduleName, const std::unordered_set<std::string>& topLevelNames) const {
    switch (decl.kind) {
        case DeclKind::Import:
            break;
        case DeclKind::Struct: {
            decl.name = moduleName + "." + decl.name;
            auto& structDecl = static_cast<StructDecl&>(decl);
            for (auto& field : structDecl.fields) {
                qualifyType(field.type, moduleName, topLevelNames);
                if (field.defaultValue) {
                    qualifyExpr(*field.defaultValue, moduleName, topLevelNames, {});
                }
            }
            break;
        }
        case DeclKind::Enum: {
            decl.name = moduleName + "." + decl.name;
            auto& enumDecl = static_cast<EnumDecl&>(decl);
            for (auto& param : enumDecl.parameters) {
                qualifyType(param.type, moduleName, topLevelNames);
            }
            for (auto& element : enumDecl.elements) {
                for (auto& payloadType : element.payloadTypes) {
                    qualifyType(payloadType, moduleName, topLevelNames);
                }
                for (auto& payloadValue : element.payloadValues) {
                    qualifyExpr(*payloadValue, moduleName, topLevelNames, {});
                }
            }
            break;
        }
        case DeclKind::Class: {
            decl.name = moduleName + "." + decl.name;
            auto& classDecl = static_cast<ClassDecl&>(decl);
            for (auto& included : classDecl.includedStructs) {
                if (!isQualified(included) && topLevelNames.contains(included)) {
                    included = moduleName + "." + included;
                }
            }
            for (auto& member : classDecl.members) {
                qualifyType(member.type, moduleName, topLevelNames);
                if (member.dynamicValue) {
                    qualifyExpr(*member.dynamicValue, moduleName, topLevelNames, {});
                }
            }
            for (auto& method : classDecl.methods) {
                auto& functionDecl = static_cast<FunctionDecl&>(*method);
                functionDecl.receiverType = decl.name;
                qualifyDecl(*method, moduleName, topLevelNames);
            }
            break;
        }
        case DeclKind::Function: {
            auto& functionDecl = static_cast<FunctionDecl&>(decl);
            if (functionDecl.receiverType.empty()) {
                decl.name = moduleName + "." + decl.name;
            }
            for (auto& param : functionDecl.compileParameters) {
                qualifyType(param.type, moduleName, topLevelNames);
            }
            std::unordered_set<std::string> locals;
            for (auto& param : functionDecl.runtimeParameters) {
                qualifyType(param.type, moduleName, topLevelNames);
                locals.insert(param.name);
            }
            for (auto& returnType : functionDecl.returnTypes) {
                qualifyType(returnType, moduleName, topLevelNames);
            }
            if (!functionDecl.receiverType.empty() && !isQualified(functionDecl.receiverType)) {
                functionDecl.receiverType = moduleName + "." + functionDecl.receiverType;
            }
            if (functionDecl.body) {
                qualifyStmt(*functionDecl.body, moduleName, topLevelNames, locals);
            }
            break;
        }
    }
}

bool ModuleLoader::isQualified(const std::string& name) const {
    return name.find('.') != std::string::npos;
}

std::filesystem::path ModuleLoader::modulePathFor(const std::filesystem::path& projectRoot, const ImportDecl& importDecl) const {
    std::filesystem::path path = projectRoot;
    for (const auto& segment : importDecl.moduleSegments) {
        path /= segment;
    }
    path.replace_extension(".ax");
    return path;
}

std::string ModuleLoader::joinSegments(const std::vector<std::string>& segments) const {
    std::string value;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            value += '.';
        }
        value += segments[i];
    }
    return value;
}

std::unordered_set<std::string> ModuleLoader::collectTopLevelNames(const TranslationUnit& unit) const {
    std::unordered_set<std::string> names;
    for (const auto& decl : unit.declarations) {
        if (decl->kind != DeclKind::Import) {
            names.insert(decl->name);
        }
    }
    return names;
}

bool ModuleLoader::isBuiltinTypeName(const std::string& name) const {
    static const std::unordered_set<std::string> builtinNames {
        "int", "void", "str", "error", "bool", "i2", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "short", "long", "double", "float", "f8", "f16", "f32", "f64", "char", "null"
    };
    return builtinNames.contains(name);
}

}  // namespace axc
