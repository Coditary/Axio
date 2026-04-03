#include "axc/Lex/Lexer.h"

namespace axc {

Lexer::Lexer(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics) {}

}  // namespace axc
