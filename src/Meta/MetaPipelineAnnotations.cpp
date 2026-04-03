#include "axc/Meta/MetaPipeline.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

void MetaPipeline::validateAnnotations(TranslationUnit& translationUnit) const {
    for (const auto& declaration : translationUnit.declarations) {
        for (const Annotation& annotation : declaration->annotations) {
            if (annotation.name == "inline" || annotation.name == "entry" || annotation.name == "trace" || annotation.name == "ThreadSafe") {
                continue;
            }
            diagnostics_.warning(annotation.range, "unknown annotation '@" + annotation.name + "' ignored");
        }
    }
}

}  // namespace axc
