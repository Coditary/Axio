#pragma once

#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

class ModuleImportResolver {
  public:
    ModuleImportResolver(DiagnosticEngine& diagnostics,
                         const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    ModuleImportBindings collectBindings(const TranslationUnit& unit) const;

  private:
    DiagnosticEngine& diagnostics_;
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
