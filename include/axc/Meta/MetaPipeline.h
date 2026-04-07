#pragma once

#include "axc/AST/AST.h"

namespace axc {

namespace detail {
class MetaWorkflow;
}

class DiagnosticEngine;
class SourceManager;

class MetaPipeline {
  public:
    MetaPipeline(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    void run(TranslationUnit& translationUnit) const;

  private:
    friend class detail::MetaWorkflow;

    void validateAnnotations(TranslationUnit& translationUnit) const;
    void validateEmbedCalls(TranslationUnit& translationUnit) const;

    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
