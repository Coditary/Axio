#pragma once

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;
class SourceManager;

class MetaPipeline {
  public:
    MetaPipeline(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    void run(TranslationUnit& translationUnit) const;

  private:
    void validateAnnotations(TranslationUnit& translationUnit) const;
    void validateEmbedCalls(TranslationUnit& translationUnit) const;

    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
