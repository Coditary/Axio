#include "ModuleEmissionWorkflow.h"

#include "../Internal/LLVMEmitterInternal.h"
#include "axc/Support/SourceManager.h"

namespace axc::detail {

ModuleEmissionWorkflow::ModuleEmissionWorkflow(ModuleEmitter& emitter) : emitter_(emitter) {}

std::unique_ptr<llvm::Module> ModuleEmissionWorkflow::emit(const TranslationUnit& translationUnit) {
    emitter_.module_->setSourceFileName(emitter_.sourceManager_.path().string());
    collectClassMetadata(translationUnit);
    collectEnums(translationUnit);
    declareAggregates(translationUnit);
    declareCallables(translationUnit);
    defineCallables(translationUnit);
    return std::move(emitter_.module_);
}

void ModuleEmissionWorkflow::collectClassMetadata(const TranslationUnit& translationUnit) {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind != DeclKind::Class) {
            continue;
        }
        const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
        emitter_.classFieldNames_[classDecl.name];
        emitter_.classMethodNames_[classDecl.name];
        for (const auto& member : classDecl.members) {
            emitter_.classFieldNames_[classDecl.name].insert(member.name);
        }
        for (const auto& method : classDecl.methods) {
            emitter_.classMethodNames_[classDecl.name].insert(method->name);
        }
    }
}

void ModuleEmissionWorkflow::collectEnums(const TranslationUnit& translationUnit) {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Enum) {
            emitter_.collectEnum(*static_cast<const EnumDecl*>(declaration.get()));
        }
    }
}

void ModuleEmissionWorkflow::declareAggregates(const TranslationUnit& translationUnit) {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Struct) {
            emitter_.declareStruct(*static_cast<const StructDecl*>(declaration.get()));
        }
        if (declaration->kind == DeclKind::Class) {
            emitter_.declareClass(*static_cast<const ClassDecl*>(declaration.get()));
        }
    }
}

void ModuleEmissionWorkflow::declareCallables(const TranslationUnit& translationUnit) {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::GlobalVar) {
            emitter_.declareGlobal(*static_cast<const GlobalVarDecl*>(declaration.get()));
        } else if (declaration->kind == DeclKind::Function) {
            emitter_.declareFunction(*static_cast<const FunctionDecl*>(declaration.get()));
        } else if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            for (const auto& method : classDecl.methods) {
                emitter_.declareFunction(static_cast<const FunctionDecl&>(*method));
            }
        }
    }
}

void ModuleEmissionWorkflow::defineCallables(const TranslationUnit& translationUnit) {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind == DeclKind::Function) {
            emitter_.defineFunction(*static_cast<const FunctionDecl*>(declaration.get()));
        } else if (declaration->kind == DeclKind::Class) {
            const auto& classDecl = static_cast<const ClassDecl&>(*declaration);
            for (const auto& method : classDecl.methods) {
                emitter_.defineFunction(static_cast<const FunctionDecl&>(*method));
            }
        }
    }
}

}  // namespace axc::detail
