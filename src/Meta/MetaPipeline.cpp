#include "axc/Meta/MetaPipeline.h"

namespace axc {

MetaPipeline::MetaPipeline(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics) {}

void MetaPipeline::run(TranslationUnit& translationUnit) const {
    validateAnnotations(translationUnit);
    validateEmbedCalls(translationUnit);
}

}  // namespace axc
