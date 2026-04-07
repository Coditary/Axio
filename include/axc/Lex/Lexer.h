#pragma once

#include <vector>

#include "axc/Lex/Token.h"

namespace axc {

class DiagnosticEngine;
class SourceManager;

/// @brief Converts source text into a flat token stream.
class Lexer {
  public:
    /// @brief Bind the lexer to one loaded source file and diagnostic engine.
    Lexer(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    /// @brief Lex the entire input file into tokens.
    std::vector<Token> lex();

  private:
    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
