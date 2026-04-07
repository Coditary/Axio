#include "axc/Meta/MetaPipeline.h"

#include "MetaWorkflow.h"

namespace axc {

MetaPipeline::MetaPipeline(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics) {}

void MetaPipeline::run(TranslationUnit& translationUnit) const {
    detail::MetaWorkflow(*this).run(translationUnit);
}

}  // namespace axc
