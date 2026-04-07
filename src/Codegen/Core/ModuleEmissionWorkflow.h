#pragma once

#include <memory>

#include <llvm/IR/Module.h>

#include "axc/AST/AST.h"

namespace axc {

class ModuleEmitter;

namespace detail {

class ModuleEmissionWorkflow {
  public:
    explicit ModuleEmissionWorkflow(ModuleEmitter& emitter);

    std::unique_ptr<llvm::Module> emit(const TranslationUnit& translationUnit);

  private:
    void collectClassMetadata(const TranslationUnit& translationUnit);
    void collectEnums(const TranslationUnit& translationUnit);
    void declareAggregates(const TranslationUnit& translationUnit);
    void declareCallables(const TranslationUnit& translationUnit);
    void defineCallables(const TranslationUnit& translationUnit);

    ModuleEmitter& emitter_;
};

}  // namespace detail

}  // namespace axc
