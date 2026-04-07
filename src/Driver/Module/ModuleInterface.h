#pragma once

#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

/// @brief Builds a stable public module interface from a translation unit.
class ModuleInterfaceBuilder {
  public:
    /// @brief Create a builder using all already-known module interfaces.
    ModuleInterfaceBuilder(DiagnosticEngine& diagnostics,
                           const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    /// @brief Populate `interface` from `unit` and compute its API fingerprint.
    bool build(const TranslationUnit& unit, const std::string& moduleName, ModuleInterface& interface) const;

  private:
    /// @brief Produce a stable signature string for one declaration.
    std::string declSignature(const Decl& decl) const;
    /// @brief Produce a stable signature string for one type spelling.
    std::string typeSignature(const Type& type) const;
    /// @brief Add one exported symbol to the interface, reporting duplicates.
    void addExport(ModuleInterface& interface, const ModuleSymbol& symbol, SourceRange range) const;
    /// @brief Finalize the interface fingerprint after all exports were collected.
    void finalizeFingerprint(ModuleInterface& interface) const;

    /// Shared diagnostic sink.
    DiagnosticEngine& diagnostics_;
    /// Already known interfaces used for validation and cross-module checks.
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
