#include "MetaWorkflow.h"

#include "axc/Meta/MetaPipeline.h"

namespace axc::detail {

MetaWorkflow::MetaWorkflow(const MetaPipeline& pipeline) : pipeline_(pipeline) {}

void MetaWorkflow::run(TranslationUnit& translationUnit) const {
    pipeline_.validateAnnotations(translationUnit);
    pipeline_.validateEmbedCalls(translationUnit);
}

}  // namespace axc::detail
