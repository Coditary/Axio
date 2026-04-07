#pragma once

#include <memory>

#include <llvm/IR/Module.h>

#include "axc/AST/AST.h"

namespace axc {

class ModuleEmitter;

namespace detail {

/// @brief Staged workflow that drives declaration and definition emission.
class ModuleEmissionWorkflow {
  public:
    /// @brief Create the workflow around one mutable module emitter.
    explicit ModuleEmissionWorkflow(ModuleEmitter& emitter);

    /// @brief Emit a full LLVM module for the given translation unit.
    std::unique_ptr<llvm::Module> emit(const TranslationUnit& translationUnit);

  private:
    /// @brief Precompute class/member metadata used by later declaration and expression lowering.
    void collectClassMetadata(const TranslationUnit& translationUnit);
    /// @brief Precompute enum metadata used by constant lowering.
    void collectEnums(const TranslationUnit& translationUnit);
    /// @brief Declare structs and classes before any references are lowered.
    void declareAggregates(const TranslationUnit& translationUnit);
    /// @brief Declare functions and globals before bodies are emitted.
    void declareCallables(const TranslationUnit& translationUnit);
    /// @brief Emit executable bodies for declared callables.
    void defineCallables(const TranslationUnit& translationUnit);

    /// Mutable lowering engine shared across workflow stages.
    ModuleEmitter& emitter_;
};

}  // namespace detail

}  // namespace axc
