#pragma once

#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

class ModuleInterfaceBuilder {
  public:
    ModuleInterfaceBuilder(DiagnosticEngine& diagnostics,
                           const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    bool build(const TranslationUnit& unit, const std::string& moduleName, ModuleInterface& interface) const;

  private:
    std::string declSignature(const Decl& decl) const;
    std::string typeSignature(const Type& type) const;
    void addExport(ModuleInterface& interface, const ModuleSymbol& symbol, SourceRange range) const;
    void finalizeFingerprint(ModuleInterface& interface) const;

    DiagnosticEngine& diagnostics_;
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
