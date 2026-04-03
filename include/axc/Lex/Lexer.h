#pragma once

#include <vector>

#include "axc/Lex/Token.h"

namespace axc {

class DiagnosticEngine;
class SourceManager;

class Lexer {
  public:
    Lexer(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    std::vector<Token> lex();

  private:
    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
