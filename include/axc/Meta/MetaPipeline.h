#pragma once

#include "axc/AST/AST.h"

namespace axc {

namespace detail {
class MetaWorkflow;
}

class DiagnosticEngine;
class SourceManager;

/// @brief Validation and transformation stage for meta-language features.
class MetaPipeline {
  public:
    /// @brief Create a meta pipeline over one source manager and diagnostic sink.
    MetaPipeline(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    /// @brief Run all configured meta validations over a translation unit.
    void run(TranslationUnit& translationUnit) const;

  private:
    friend class detail::MetaWorkflow;

    void validateAnnotations(TranslationUnit& translationUnit) const;
    void validateEmbedCalls(TranslationUnit& translationUnit) const;

    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
