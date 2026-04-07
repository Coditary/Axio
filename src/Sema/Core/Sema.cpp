#include "axc/Sema/Sema.h"

#include "../Internal/SemaInternal.h"

namespace axc {

Sema::Sema(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool Sema::analyze(TranslationUnit& translationUnit) const {
    SemaImpl impl(diagnostics_);
    return impl.analyze(translationUnit);
}

}  // namespace axc
