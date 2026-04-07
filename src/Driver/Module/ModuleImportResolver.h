#pragma once

#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

/// @brief Computes visible names and module aliases from import declarations.
class ModuleImportResolver {
  public:
    /// @brief Create a resolver using the currently known module interfaces.
    ModuleImportResolver(DiagnosticEngine& diagnostics,
                         const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    /// @brief Build import bindings for one translation unit.
    ModuleImportBindings collectBindings(const TranslationUnit& unit) const;

  private:
    /// Shared diagnostic sink.
    DiagnosticEngine& diagnostics_;
    /// Public interfaces of all currently known modules.
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
